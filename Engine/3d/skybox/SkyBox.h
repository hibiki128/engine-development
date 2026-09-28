#pragma once
#include <array>
#include <camera/projection/ViewProjection.h>
#include <cstdint>
#include <render/RenderView.h>
#include <wrl.h>
#include <d3d12.h>
#include <type/Matrix4x4.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <vector>
namespace Hagine {
class PipelineManager;
class DirectXCommon;
class SrvManager;
class SkyBox
{
  private:
    /// ===========================================
    /// private method
    /// ===========================================
    SkyBox() = default;
    ~SkyBox() = default;
    SkyBox(SkyBox &) = delete;
    SkyBox &operator=(SkyBox &) = delete;

    void Update(const ViewProjection &viewProjection);
    void CreateShape();
    void CreateVertex();
    void CreateIndex();
    void CreateSkyBox();
    void CreateCamera();

  public:
    /// ============================================
    /// public method
    /// ============================================

    void Initialize(std::string filePath);
    void Draw(const ViewProjection &viewProjection);
    static SkyBox *GetInstance()
    {
        static SkyBox instance;
        return &instance;
    }
    void Finalize();
    uint32_t GetTextureIndex() const { return textureIndex_; }

    /// <summary>
    /// 環境マップ（キューブマップ）のファイルパス。
    /// 既存のSRVはコピーできないので、別のテーブルへ差したい側が
    /// このパスから自分でSRVを作り直すために使う
    /// </summary>
    /// <returns>const std::string&: 読み込んだファイルパス</returns>
    const std::string &GetTextureFilePath() const { return textureFilePath_; }

  private:
    /// <summary>
    /// 背景ボックスの頂点データ
    /// </summary>
    struct SkyBoxVertexData3D
    {
        Vector4 position;
    };

    /// <summary>
    /// GPUに送る背景ボックスのデータ
    /// </summary>
    struct SkyBoxDataForGPU
    {
        Matrix4x4 worldMatrix;
    };

    /// <summary>
    /// GPUに送るカメラデータ
    /// </summary>
    struct CameraDataForGPU
    {
        Matrix4x4 viewProjection;
        Vector3 worldPosition;
        // パディング（16バイト境界に合わせる）
        float padding;
    };

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;
    PipelineManager *pPsoManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
    SkyBoxVertexData3D *pVertexData_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    std::vector<SkyBoxVertexData3D> vertices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr;

    uint32_t *pIndexData_ = nullptr;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    std::vector<uint32_t> indices_;

    Microsoft::WRL::ComPtr<ID3D12Resource> skyBoxResource_ = nullptr;
    SkyBoxDataForGPU *pSkyBoxData_ = nullptr;

    // カメラ用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_ = nullptr;
    CameraDataForGPU *pCameraData_ = nullptr;

  public:
    /// <summary>描ける状態か（Initialize 済みか。シーンによっては空を使わない）</summary>
    bool IsReady() const { return vertexResource_ != nullptr; }

  private:
    // カメラビュー窓（RenderView 1〜）用のカメラ行列。使われたら作る
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, RenderView::kMaxViews> viewCameraResources_{};
    std::array<CameraDataForGPU *, RenderView::kMaxViews> pViewCameraData_{};

    uint32_t textureIndex_ = 0;
    std::string textureFilePath_; // 読み込んだキューブマップのパス
};
} // namespace Hagine
