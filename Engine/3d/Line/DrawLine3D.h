#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "Graphics/PipeLine/PipeLineManager.h"
#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>
#include <type/Matrix4x4.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <wrl/client.h>

namespace Hagine {
/// <summary>
/// 3D線描画クラス
/// デバッグ用の線、グリッド、図形を描画する
/// </summary>
class DrawLine3D {
  public:
    /// ===================================================
    /// public constant
    /// ===================================================

    static const UINT kMaxLineCount = 65536; // 最大線数
    static const UINT kVertexCountLine = 2;  // 線の頂点数
    static const UINT kIndexCountLine = 0;   // 線のインデックス数

    /// ===================================================
    /// public struct
    /// ===================================================

    /// <summary>
    /// 頂点データ（座標と色）
    /// </summary>
    struct VertexPosColor {
        Vector3 pos;   // 座標
        Vector4 color; // 色
    };

    /// <summary>
    /// 線描画用データ
    /// </summary>
    struct LineData {
        Microsoft::WRL::ComPtr<ID3D12Resource> vertBuffer;  // 頂点バッファ
        Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer; // インデックスバッファ
        D3D12_VERTEX_BUFFER_VIEW vbView{};  // 頂点バッファビュー
        D3D12_INDEX_BUFFER_VIEW ibView{};   // インデックスバッファビュー
        VertexPosColor *vertMap = nullptr;  // 頂点マップ
        uint16_t *indexMap = nullptr;       // インデックスマップ
    };

    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns>DrawLine3D*: インスタンスのポインタ</returns>
    static DrawLine3D* GetInstance() {
        static DrawLine3D instance;
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
    /// メッシュ作成
    /// </summary>
    /// <param name="vertexCount">頂点数</param>
    /// <param name="indexCount">インデックス数</param>
    /// <returns>std::unique_ptr<LineData>: 作成した線データ</returns>
    std::unique_ptr<LineData> CreateMesh(UINT vertexCount, UINT indexCount);

    /// <summary>
    /// 線の端点を設定
    /// </summary>
    /// <param name="p1">始点</param>
    /// <param name="p2">終点</param>
    /// <param name="color">色</param>
    void SetPoints(const Vector3 &p1, const Vector3 &p2, const Vector4 &color = {1.0f, 1.0f, 1.0f, 1.0f});

    /// <summary>
    /// リセット
    /// </summary>
    void Reset();

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void Draw(const ViewProjection &viewProjection);

    /// <summary>
    /// グリッド描画
    /// </summary>
    /// <param name="y">Y座標</param>
    /// <param name="division">分割数</param>
    /// <param name="size">サイズ</param>
    /// <param name="color">色</param>
    void DrawGrid(float y, int division, float size, Vector4 color = {0.5f, 0.5f, 0.5f, 1.0f});

    /// <summary>
    /// 球体描画
    /// </summary>
    /// <param name="position">位置</param>
    /// <param name="color">色</param>
    /// <param name="radius">半径</param>
    /// <param name="divisions">分割数</param>
    void DrawSphere(const Vector3 &position, const Vector4 &color, float radius, int divisions);

    /// <summary>
    /// 立方体描画
    /// </summary>
    /// <param name="position">位置</param>
    /// <param name="color">色</param>
    /// <param name="size">サイズ</param>
    void DrawCube(const Vector3 &position, const Vector4 &color, float size);

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    DrawLine3D() = default;
    ~DrawLine3D() = default;
    DrawLine3D(DrawLine3D &) = delete;
    DrawLine3D &operator=(DrawLine3D &) = delete;

    /// <summary>
    /// メッシュ作成
    /// </summary>
    void CreateMeshes();

    /// <summary>
    /// リソース作成
    /// </summary>
    void CreateResource();

  private:
    /// ===================================================
    /// private struct
    /// ===================================================

    /// <summary>
    /// 定数バッファ
    /// </summary>
    struct CBuffer {
        Matrix4x4 viewProject; // ビュープロジェクション行列
    };

  private:
    /// ===================================================
    /// private varians
    /// ===================================================

    std::unique_ptr<LineData> line_; // 線データ
    uint32_t indexLine_ = 0;         // 線のインデックス

    DirectXCommon *dxCommon_ = nullptr;      // DirectX共通クラス
    PipeLineManager *psoManager_ = nullptr; // パイプラインマネージャー

    Microsoft::WRL::ComPtr<ID3D12Resource> cBufferResource_ = nullptr; // 定数バッファリソース
    CBuffer *cBufferData_ = nullptr;                   // 定数バッファデータ
};
} // namespace Hagine
