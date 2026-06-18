#pragma once
#include <Camera/ViewProjection/ViewProjection.h>
#include <cstdint>
#include <d3d12.h>
#include <type/Matrix4x4.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <vector>
namespace Hagine {
class PipeLineManager;
class DirectXCommon;
class SrvManager;
class SkyBox {
  private:
    /// ===========================================
    /// private methods
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
    /// public methods
    /// ============================================

    void Initialize(std::string filePath);
    void Draw(const ViewProjection &viewProjection);
    static SkyBox* GetInstance() {
        static SkyBox instance;
        return &instance;
    }
    void Finalize();
    uint32_t GetTextureIndex() const { return textureIndex_; }

  private:
    /// <summary>
    /// 背景ボックスの頂点データ
    /// </summary>
    struct SkyBoxVertexData3D {
        Vector4 position;
    };

    /// <summary>
    /// GPUに送る背景ボックスのデータ
    /// </summary>
    struct SkyBoxDataForGPU {
        Matrix4x4 worldMatrix;
    };

    /// <summary>
    /// GPUに送るカメラデータ
    /// </summary>
    struct CameraDataForGPU {
        Matrix4x4 viewProjection;
        Vector3 worldPosition;
        // パディング（16バイト境界に合わせる）
        float padding;
    };

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    PipeLineManager *psoManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
    SkyBoxVertexData3D *vertexData_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    std::vector<SkyBoxVertexData3D> vertices_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr;

    uint32_t *indexData_ = nullptr;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    std::vector<uint32_t> indices_;

    Microsoft::WRL::ComPtr<ID3D12Resource> skyBoxResource_ = nullptr;
    SkyBoxDataForGPU *skyBoxData_ = nullptr;

    // カメラ用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_ = nullptr;
    CameraDataForGPU *cameraData_ = nullptr;

    uint32_t textureIndex_ = 0;
};
} // namespace Hagine
