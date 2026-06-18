#include "DrawLine3D.h"
#include "DirectXCommon.h"
#include <myMath.h>


namespace Hagine {
std::unique_ptr<DrawLine3D::LineData> DrawLine3D::CreateMesh(UINT vertexCount, UINT indexCount) {

    std::unique_ptr<LineData> mesh = std::make_unique<LineData>();

    UINT vertBufferSize = sizeof(VertexPosColor) * vertexCount;
    mesh->vertBuffer = dxCommon_->CreateBufferResource(vertBufferSize);

    mesh->vbView.BufferLocation = mesh->vertBuffer->GetGPUVirtualAddress();
    mesh->vbView.StrideInBytes = sizeof(VertexPosColor);
    mesh->vbView.SizeInBytes = vertBufferSize;

    D3D12_RANGE readRange = {0, 0};
    mesh->vertBuffer->Map(0, &readRange, reinterpret_cast<void **>(&mesh->vertMap));

    UINT indexBufferSize = sizeof(uint16_t) * indexCount;
    if (indexCount > 0) {
        mesh->indexBuffer = dxCommon_->CreateBufferResource(indexBufferSize);

        mesh->ibView.BufferLocation = mesh->indexBuffer->GetGPUVirtualAddress();
        mesh->ibView.Format = DXGI_FORMAT_R16_UINT;
        mesh->ibView.SizeInBytes = indexBufferSize;

        mesh->indexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&mesh->indexMap));
    }

    return mesh;
}

void DrawLine3D::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    CreateMeshes();
    CreateResource();
    psoManager_ = PipeLineManager::GetInstance();
}

void DrawLine3D::Finalize() {
    cBufferResource_.Reset();
    if (line_) {
        line_->vertBuffer.Reset();
        line_->indexBuffer.Reset();
        line_.reset();
    }
}

void DrawLine3D::SetPoints(const Vector3 &p1, const Vector3 &p2, const Vector4 &color) {

    if (indexLine_ < kMaxLineCount) {
        line_->vertMap[indexLine_ * 2] = {p1, color};
        line_->vertMap[indexLine_ * 2 + 1] = {p2, color};

        indexLine_++;
    }
}

void DrawLine3D::Reset() {
    indexLine_ = 0;
}

void DrawLine3D::Draw(const ViewProjection &viewProjection) {

    if (indexLine_ == 0) {
        return;
    }
    cBufferData_->viewProject = viewProjection.matView_ * viewProjection.matProjection_;

    psoManager_->DrawCommonSetting(PipelineType::kLine3d);
    D3D12_VERTEX_BUFFER_VIEW vbView = line_->vbView;
    dxCommon_->GetCommandList()->IASetVertexBuffers(0, 1, &vbView);
    dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(0, cBufferResource_->GetGPUVirtualAddress());
    dxCommon_->GetCommandList()->DrawInstanced(indexLine_ * 2, 1, 0, 0);

    Reset();
}

void DrawLine3D::DrawGrid(float y, int division, float size, Vector4 color) {
    // 分割数が無効な場合は何もしない
    if (division <= 0 || size <= 0.0f) {
        return;
    }

    // 一区画の幅
    float interval = (size * 2.0f) / division;

    // グリッド線の色（薄いグレー）
    const Vector4 gridColor = color;

    for (int i = 0; i <= division; ++i) {
        // 現在の位置
        float offset = -size + i * interval;

        // X方向の線（Zを移動）
        Vector3 startX = {-size, y, offset};
        Vector3 endX = {size, y, offset};
        SetPoints(startX, endX, gridColor);

        // Z方向の線（Xを移動）
        Vector3 startZ = {offset, y, -size};
        Vector3 endZ = {offset, y, size};
        SetPoints(startZ, endZ, gridColor);
    }
}

void DrawLine3D::CreateMeshes() {

    const UINT maxVertex = kMaxLineCount * kVertexCountLine;
    const UINT maxIndices = 20;

    line_ = CreateMesh(maxVertex, maxIndices);
}

void DrawLine3D::CreateResource() {
    cBufferResource_ = dxCommon_->CreateBufferResource(sizeof(CBuffer));
    cBufferData_ = nullptr;
    cBufferResource_->Map(0, nullptr, reinterpret_cast<void **>(&cBufferData_));
    cBufferData_->viewProject = MakeIdentity4x4();
}

void DrawLine3D::DrawCube(const Vector3 &position, const Vector4 &color, float size) {
    // 半サイズ（中心から各辺までの距離）
    float hs = size * 0.5f;

    // 立方体の8頂点（左手座標系）
    Vector3 corners[8] = {
        {position.x - hs, position.y - hs, position.z - hs}, // 0: 左下前
        {position.x + hs, position.y - hs, position.z - hs}, // 1: 右下前
        {position.x + hs, position.y + hs, position.z - hs}, // 2: 右上前
        {position.x - hs, position.y + hs, position.z - hs}, // 3: 左上前
        {position.x - hs, position.y - hs, position.z + hs}, // 4: 左下後
        {position.x + hs, position.y - hs, position.z + hs}, // 5: 右下後
        {position.x + hs, position.y + hs, position.z + hs}, // 6: 右上後
        {position.x - hs, position.y + hs, position.z + hs}, // 7: 左上後
    };

    // 各辺を線で結ぶ（合計12本）
    int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // 前面
        {4, 5},
        {5, 6},
        {6, 7},
        {7, 4}, // 背面
        {0, 4},
        {1, 5},
        {2, 6},
        {3, 7} // 側面
    };

    for (int i = 0; i < 12; ++i) {
        SetPoints(corners[edges[i][0]], corners[edges[i][1]], color);
    }
}

void DrawLine3D::DrawSphere(const Vector3 &position, const Vector4 &color, float radius, int divisions) {
    // 分割数が最低限必要
    if (divisions < 3)
        return;

    const float PI = 3.1415926535f;

    // 緯度方向（Y軸周り）の分割
    for (int i = 0; i <= divisions; ++i) {
        float theta1 = PI * (float)(i) / divisions - PI / 2.0f;
        float theta2 = PI * (float)(i + 1) / divisions - PI / 2.0f;

        for (int j = 0; j <= divisions; ++j) {
            float phi = 2.0f * PI * (float)(j) / divisions;

            // 緯線用の点1・点2
            Vector3 p1 = {
                position.x + radius * cosf(theta1) * cosf(phi),
                position.y + radius * sinf(theta1),
                position.z + radius * cosf(theta1) * sinf(phi)};

            Vector3 p2 = {
                position.x + radius * cosf(theta2) * cosf(phi),
                position.y + radius * sinf(theta2),
                position.z + radius * cosf(theta2) * sinf(phi)};

            SetPoints(p1, p2, color);
        }
    }

    // 経度方向（Z軸周り）の分割
    for (int j = 0; j <= divisions; ++j) {
        float phi1 = 2.0f * PI * (float)(j) / divisions;
        float phi2 = 2.0f * PI * (float)(j + 1) / divisions;

        for (int i = 0; i <= divisions; ++i) {
            float theta = PI * (float)(i) / divisions - PI / 2.0f;

            Vector3 p1 = {
                position.x + radius * cosf(theta) * cosf(phi1),
                position.y + radius * sinf(theta),
                position.z + radius * cosf(theta) * sinf(phi1)};

            Vector3 p2 = {
                position.x + radius * cosf(theta) * cosf(phi2),
                position.y + radius * sinf(theta),
                position.z + radius * cosf(theta) * sinf(phi2)};

            SetPoints(p1, p2, color);
        }
    }
}
} // namespace Hagine
