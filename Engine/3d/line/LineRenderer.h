#pragma once
#include "camera/projection/ViewProjection.h"
#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <type/Matrix4x4.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

namespace Hagine {
class DirectXCommon;
class PipelineManager;

/// <summary>
/// 線の頂点（16バイト）
/// 色はRGBA8へ詰めることで、旧実装の28バイト構成から転送量をほぼ半減させている
/// </summary>
struct LineVertex
{
    Vector3 position; // 座標
    uint32_t color;   // RGBA8（PackLineColorで生成）
};
static_assert(sizeof(LineVertex) == 16, "LineVertexは16バイトである必要がある");

/// <summary>
/// Vector4の色をRGBA8へ詰める
/// </summary>
/// <param name="color">0〜1の色</param>
/// <returns>uint32_t: RGBA8へ詰めた色</returns>
inline uint32_t PackLineColor(const Vector4 &color)
{
    auto to8 = [](float v) -> uint32_t {
        // 0〜1へクランプしてから8bit化する
        const float clamped = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
    };
    return to8(color.x) | (to8(color.y) << 8) | (to8(color.z) << 16) | (to8(color.w) << 24);
}

/// <summary>
/// 静的バッチの識別子（0は無効値）
/// </summary>
using LineBatchId = uint32_t;
inline constexpr LineBatchId kInvalidLineBatch = 0;

/// <summary>
/// 3D線描画クラス
///
/// 線を2系統に分けて扱う。
///   ・動的線  : 毎フレーム積み直す線。CPU側のキャッシュ可能メモリへ詰めてから
///               1回のmemcpyでアップロードバッファへ流し込み、1ドローで描画する。
///   ・静的バッチ: 形が変わらない線（地形メッシュのワイヤーフレーム、グリッドなど）。
///               ローカル座標のままGPUへ常駐させ、毎フレームはワールド行列と色だけを
///               ルート定数で差し替えて描画する。CPUコストはドロー1回ぶんのみ。
///
/// 頂点バッファはフレームリング（kRingSize枚）で持ち、CPUがGPUの読み取り中バッファを
/// 上書きしないようにしている。
/// </summary>
class LineRenderer
{
  public:
    /// ===================================================
    /// public constant
    /// ===================================================

    static constexpr uint32_t kRingSize = 3;              // 頂点バッファのリング枚数
    static constexpr uint32_t kInitialLineCapacity = 8192; // 動的線の初期容量（本数）

    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns>LineRenderer*: インスタンスのポインタ</returns>
    static LineRenderer *GetInstance()
    {
        static LineRenderer instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// フレーム開始。カリング用の視錐台を更新し、前フレームの積み上げを破棄する
    /// </summary>
    /// <param name="viewProjection">このフレームのビュープロジェクション</param>
    void BeginFrame(const ViewProjection &viewProjection);

    /// <summary>
    /// 積み上げた線を描画する。描画後は積み上げをリセットする
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void Render(const ViewProjection &viewProjection);

    /// <summary>
    /// 積み上げた線を外部提供のビュープロジェクション定数バッファで描画する（リセットしない）
    /// GPUパーティクルのプレビュー窓など、別カメラで同じ線を再表示するために使う。
    /// RT・ビューポート・ディスクリプタヒープは呼び出し側で設定済みであること。
    /// </summary>
    /// <param name="pCommandList">記録先コマンドリスト</param>
    /// <param name="viewProjCB">viewProject行列を格納した定数バッファのGPUアドレス</param>
    void RenderWithExternalCamera(ID3D12GraphicsCommandList *pCommandList, D3D12_GPU_VIRTUAL_ADDRESS viewProjCB);

    /// <summary>
    /// Line3d パイプラインを直接使う外部コード向けに、ルート定数（ワールド行列＋乗算色）を設定する。
    /// このパイプラインで描く前に必ず1回呼ぶ必要がある。
    /// </summary>
    /// <param name="pCommandList">記録先コマンドリスト</param>
    /// <param name="world">ワールド行列</param>
    /// <param name="tint">頂点色に乗算する色</param>
    static void SetDrawConstants(ID3D12GraphicsCommandList *pCommandList, const Matrix4x4 &world, const Vector4 &tint);

    /// ===================================================
    /// 動的線の積み上げ
    /// ===================================================

    /// <summary>
    /// 線を1本積む
    /// </summary>
    /// <param name="start">始点</param>
    /// <param name="end">終点</param>
    /// <param name="color">色</param>
    void AddLine(const Vector3 &start, const Vector3 &end, const Vector4 &color)
    {
        AddLinePacked(start, end, PackLineColor(color));
    }

    /// <summary>
    /// 色を詰め済みの線を1本積む（同色を大量に積む場合はこちらが速い）
    /// </summary>
    /// <param name="start">始点</param>
    /// <param name="end">終点</param>
    /// <param name="packedColor">PackLineColorで詰めた色</param>
    void AddLinePacked(const Vector3 &start, const Vector3 &end, uint32_t packedColor)
    {
        if (lineCount_ >= lineCapacity_ && !Grow())
        {
            return;
        }
        LineVertex *dst = staging_.get() + lineCount_ * 2;
        dst[0].position = start;
        dst[0].color = packedColor;
        dst[1].position = end;
        dst[1].color = packedColor;
        ++lineCount_;
    }

    /// <summary>
    /// 折れ線を積む（points[0]→points[1]→…と繋ぐ）
    /// </summary>
    /// <param name="points">点列</param>
    /// <param name="pointCount">点数</param>
    /// <param name="color">色</param>
    /// <param name="closed">終点と始点を閉じるか</param>
    void AddPolyline(const Vector3 *points, uint32_t pointCount, const Vector4 &color, bool closed = false);

    /// <summary>
    /// 軸平行境界ボックスを積む
    /// </summary>
    /// <param name="min">最小座標</param>
    /// <param name="max">最大座標</param>
    /// <param name="color">色</param>
    void AddBox(const Vector3 &min, const Vector3 &max, const Vector4 &color);

    /// <summary>
    /// 8頂点を直接指定してボックスを積む（有向境界ボックス・視錐台など）
    /// 頂点順は 0-3 が手前面（反時計回り）、4-7 が対応する奥面。
    /// </summary>
    /// <param name="corners">8頂点</param>
    /// <param name="color">色</param>
    void AddBoxCorners(const Vector3 corners[8], const Vector4 &color);

    /// <summary>
    /// 立方体を積む
    /// </summary>
    /// <param name="center">中心</param>
    /// <param name="size">一辺の長さ</param>
    /// <param name="color">色</param>
    void AddCube(const Vector3 &center, float size, const Vector4 &color);

    /// <summary>
    /// 球を積む（直交する3つの大円で表現する。視錐台外なら積まない）
    /// </summary>
    /// <param name="center">中心</param>
    /// <param name="radius">半径</param>
    /// <param name="color">色</param>
    /// <param name="segments">1周の分割数</param>
    void AddSphere(const Vector3 &center, float radius, const Vector4 &color, uint32_t segments = 16);

    /// <summary>
    /// 円を積む
    /// </summary>
    /// <param name="center">中心</param>
    /// <param name="axisU">円平面の基底1（長さが半径）</param>
    /// <param name="axisV">円平面の基底2（長さが半径）</param>
    /// <param name="color">色</param>
    /// <param name="segments">分割数</param>
    void AddCircle(const Vector3 &center, const Vector3 &axisU, const Vector3 &axisV, const Vector4 &color, uint32_t segments = 16);

    /// <summary>
    /// Y軸に沿った円柱を積む
    /// </summary>
    /// <param name="center">中心</param>
    /// <param name="radius">半径</param>
    /// <param name="halfHeight">高さの半分</param>
    /// <param name="color">色</param>
    /// <param name="segments">円周の分割数</param>
    void AddCylinder(const Vector3 &center, float radius, float halfHeight, const Vector4 &color, uint32_t segments = 16);

    /// ===================================================
    /// 静的バッチ
    /// ===================================================

    /// <summary>
    /// 静的バッチを作成する。頂点は線分リスト（2頂点で1本）のローカル座標で渡す
    /// </summary>
    /// <param name="vertices">頂点配列</param>
    /// <param name="vertexCount">頂点数（偶数）</param>
    /// <returns>LineBatchId: 作成したバッチのID（失敗時はkInvalidLineBatch）</returns>
    LineBatchId CreateBatch(const LineVertex *vertices, uint32_t vertexCount);

    /// <summary>
    /// 静的バッチの内容を作り直す（頂点数が変わってもよい）
    /// </summary>
    /// <param name="id">バッチID</param>
    /// <param name="vertices">頂点配列</param>
    /// <param name="vertexCount">頂点数（偶数）</param>
    void UpdateBatch(LineBatchId id, const LineVertex *vertices, uint32_t vertexCount);

    /// <summary>
    /// 静的バッチを破棄する
    /// </summary>
    /// <param name="id">バッチID</param>
    void DestroyBatch(LineBatchId id);

    /// <summary>
    /// このフレームで静的バッチを描画するよう予約する
    /// </summary>
    /// <param name="id">バッチID</param>
    /// <param name="world">ワールド行列</param>
    /// <param name="tint">頂点色に乗算する色</param>
    void SubmitBatch(LineBatchId id, const Matrix4x4 &world, const Vector4 &tint);

    /// ===================================================
    /// カリング
    /// ===================================================

    /// <summary>
    /// 境界球が視錐台に入っているか判定する
    /// </summary>
    /// <param name="center">中心</param>
    /// <param name="radius">半径</param>
    /// <returns>bool: 見えている可能性があればtrue</returns>
    bool IsSphereVisible(const Vector3 &center, float radius) const;

    /// ===================================================
    /// 統計
    /// ===================================================

    /// <summary>
    /// このフレームに積まれた動的線の本数を取得
    /// </summary>
    uint32_t GetDynamicLineCount() const { return lineCount_; }

    /// <summary>
    /// このフレームに予約された静的バッチ数を取得
    /// </summary>
    uint32_t GetSubmittedBatchCount() const { return static_cast<uint32_t>(batchSubmissions_.size()); }

    /// <summary>
    /// 生存している静的バッチ数を取得
    /// </summary>
    uint32_t GetLiveBatchCount() const { return static_cast<uint32_t>(batches_.size()); }

  private:
    /// ===================================================
    /// private struct
    /// ===================================================

    /// <summary>
    /// リングの1スロット
    /// </summary>
    struct RingSlot
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;  // 頂点バッファ
        D3D12_VERTEX_BUFFER_VIEW view{};                // 頂点バッファビュー
        LineVertex *mapped = nullptr;                   // 永続マップ先
        uint32_t capacityVertices = 0;                  // 確保済み頂点数
    };

    /// <summary>
    /// 静的バッチの実体
    /// </summary>
    struct StaticBatch
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer; // 頂点バッファ
        D3D12_VERTEX_BUFFER_VIEW view{};               // 頂点バッファビュー
        uint32_t vertexCount = 0;                      // 描画する頂点数
        uint32_t capacityVertices = 0;                 // 確保済み頂点数
    };

    /// <summary>
    /// 静的バッチの描画予約
    /// </summary>
    struct BatchSubmission
    {
        LineBatchId id;   // バッチID
        Matrix4x4 world;  // ワールド行列
        Vector4 tint;     // 乗算色
    };

    /// <summary>
    /// ルート定数（world行列＋乗算色）
    /// </summary>
    struct DrawConstants
    {
        Matrix4x4 world; // ワールド行列
        Vector4 tint;    // 乗算色
    };
    static constexpr uint32_t kDrawConstantsDwords = sizeof(DrawConstants) / 4;

    /// <summary>
    /// 破棄待ちリソース（GPUが読み終わるまで保持する）
    /// </summary>
    struct PendingRelease
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer; // 対象リソース
        uint32_t framesLeft = 0;                       // 残り待機フレーム数
    };

    /// ===================================================
    /// private method
    /// ===================================================

    LineRenderer() = default;
    ~LineRenderer() = default;
    LineRenderer(const LineRenderer &) = delete;
    LineRenderer &operator=(const LineRenderer &) = delete;

    /// <summary>
    /// CPU側ステージングの容量を倍にする
    /// </summary>
    /// <returns>bool: 拡張できたらtrue</returns>
    bool Grow();

    /// <summary>
    /// リングスロットが必要な頂点数を収められるよう確保し直す
    /// </summary>
    /// <param name="slot">対象スロット</param>
    /// <param name="vertexCount">必要頂点数</param>
    void EnsureRingCapacity(RingSlot &slot, uint32_t vertexCount);

    /// <summary>
    /// アップロードヒープ上の頂点バッファを生成して永続マップする
    /// </summary>
    /// <param name="vertexCount">頂点数</param>
    /// <param name="outView">生成した頂点バッファビュー</param>
    /// <param name="outMapped">マップ先（不要ならnullptr）</param>
    /// <returns>生成したリソース</returns>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateVertexBuffer(uint32_t vertexCount, D3D12_VERTEX_BUFFER_VIEW &outView, LineVertex **outMapped);

    /// <summary>
    /// 定数バッファへビュープロジェクション行列を書き込む
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void UpdateCameraBuffer(const ViewProjection &viewProjection);

    /// <summary>
    /// 描画コマンドを記録する共通処理
    /// </summary>
    /// <param name="pCommandList">記録先コマンドリスト</param>
    /// <param name="viewProjCB">ビュープロジェクション定数バッファのGPUアドレス</param>
    void RecordDrawCommands(ID3D12GraphicsCommandList *pCommandList, D3D12_GPU_VIRTUAL_ADDRESS viewProjCB);

    /// <summary>
    /// ビュープロジェクション行列から視錐台6平面を抽出する
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション行列</param>
    void ExtractFrustum(const Matrix4x4 &viewProjection);

    /// <summary>
    /// 破棄待ちリソースのカウントダウンを進め、期限切れを解放する
    /// </summary>
    void TickPendingReleases();

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    DirectXCommon *pDxCommon_ = nullptr;     // DirectX共通クラス
    PipelineManager *pPsoManager_ = nullptr; // パイプラインマネージャー

    // 動的線のCPU側ステージング（キャッシュ可能メモリ。std::vectorを使わないのは
    // Debugビルドのイテレータデバッグ経路を通さず生ポインタで詰めるため）
    std::unique_ptr<LineVertex[]> staging_;
    uint32_t lineCount_ = 0;                        // 積み上げ済みの線の本数
    uint32_t lineCapacity_ = 0;                     // 確保済みの線の本数

    RingSlot ring_[kRingSize];   // フレームリング
    uint32_t ringIndex_ = 0;     // 今フレームのスロット番号

    // 静的バッチ
    std::unordered_map<LineBatchId, StaticBatch> batches_;
    std::vector<BatchSubmission> batchSubmissions_; // このフレームの描画予約
    LineBatchId nextBatchId_ = 1;                   // 次に払い出すID

    std::vector<PendingRelease> pendingReleases_; // 破棄待ちリソース

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer_; // ビュープロジェクション定数バッファ
    Matrix4x4 *pCameraData_ = nullptr;                    // 定数バッファのマップ先

    Vector4 frustumPlanes_[6]{}; // 視錐台平面（xyz=法線, w=距離）
    bool frustumValid_ = false;  // 視錐台が有効か
};
} // namespace Hagine
