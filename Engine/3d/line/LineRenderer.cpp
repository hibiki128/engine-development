#include "LineRenderer.h"
#include "DirectXCommon.h"
#include "graphics/pipeline/PipelineManager.h"
#include <MyMath.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace Hagine {
namespace {
/// <summary>
/// 単位円のsin/cosテーブル。分割数ごとに1度だけ作って使い回す
/// （球・円・円柱の生成で毎フレーム三角関数を回さないため）
/// </summary>
struct UnitCircleTable
{
    std::vector<float> cos; // cos値
    std::vector<float> sin; // sin値
};

const UnitCircleTable &GetUnitCircle(uint32_t segments)
{
    static std::unordered_map<uint32_t, UnitCircleTable> cache;
    auto it = cache.find(segments);
    if (it != cache.end())
    {
        return it->second;
    }

    UnitCircleTable table;
    table.cos.resize(segments + 1);
    table.sin.resize(segments + 1);
    const float step = 2.0f * std::numbers::pi_v<float> / static_cast<float>(segments);
    for (uint32_t i = 0; i <= segments; ++i)
    {
        const float angle = step * static_cast<float>(i);
        table.cos[i] = std::cos(angle);
        table.sin[i] = std::sin(angle);
    }
    return cache.emplace(segments, std::move(table)).first->second;
}

/// <summary>
/// 分割数を安全な範囲へ丸める
/// </summary>
uint32_t ClampSegments(uint32_t segments)
{
    if (segments < 3)
        return 3;
    if (segments > 128)
        return 128;
    return segments;
}
} // namespace

void LineRenderer::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    pPsoManager_ = PipelineManager::GetInstance();

    // CPU側ステージングを初期容量で確保
    lineCapacity_ = kInitialLineCapacity;
    staging_ = std::make_unique<LineVertex[]>(static_cast<size_t>(lineCapacity_) * 2);

    // リングは遅延確保（最初のフレームで必要量だけ確保する）
    ringIndex_ = 0;

    // ビュープロジェクション定数バッファ
    cameraBuffer_ = pDxCommon_->CreateBufferResource(sizeof(Matrix4x4));
    cameraBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pCameraData_));
    *pCameraData_ = MakeIdentity4x4();

    batchSubmissions_.reserve(64);
}

void LineRenderer::Finalize()
{
    for (RingSlot &slot : ring_)
    {
        slot.mapped = nullptr;
        slot.buffer.Reset();
        slot.view = {};
        slot.capacityVertices = 0;
    }
    batches_.clear();
    batchSubmissions_.clear();
    pendingReleases_.clear();
    pCameraData_ = nullptr;
    cameraBuffer_.Reset();
    staging_.reset();
    lineCount_ = 0;
    lineCapacity_ = 0;
}

bool LineRenderer::Grow()
{
    // 上限を設けて暴走時のメモリ食い潰しを防ぐ（1本=32バイトなので約64MBで頭打ち）
    constexpr uint32_t kMaxLineCapacity = 2u * 1024u * 1024u;
    if (lineCapacity_ >= kMaxLineCapacity)
    {
        return false;
    }

    const uint32_t newCapacity = (lineCapacity_ == 0) ? kInitialLineCapacity
                                                      : (std::min)(lineCapacity_ * 2, kMaxLineCapacity);
    auto expanded = std::make_unique<LineVertex[]>(static_cast<size_t>(newCapacity) * 2);
    if (staging_ && lineCount_ > 0)
    {
        std::memcpy(expanded.get(), staging_.get(), static_cast<size_t>(lineCount_) * 2 * sizeof(LineVertex));
    }
    staging_ = std::move(expanded);
    lineCapacity_ = newCapacity;
    return true;
}

void LineRenderer::BeginFrame(const ViewProjection &viewProjection)
{
    lineCount_ = 0;
    batchSubmissions_.clear();
    ExtractFrustum(viewProjection.matView_ * viewProjection.matProjection_);
    TickPendingReleases();
}

void LineRenderer::AddPolyline(const Vector3 *points, uint32_t pointCount, const Vector4 &color, bool closed)
{
    if (!points || pointCount < 2)
    {
        return;
    }
    const uint32_t packed = PackLineColor(color);
    for (uint32_t i = 0; i + 1 < pointCount; ++i)
    {
        AddLinePacked(points[i], points[i + 1], packed);
    }
    if (closed)
    {
        AddLinePacked(points[pointCount - 1], points[0], packed);
    }
}

void LineRenderer::AddBox(const Vector3 &min, const Vector3 &max, const Vector4 &color)
{
    const Vector3 corners[8] = {
        {min.x, min.y, min.z},
        {max.x, min.y, min.z},
        {max.x, max.y, min.z},
        {min.x, max.y, min.z},
        {min.x, min.y, max.z},
        {max.x, min.y, max.z},
        {max.x, max.y, max.z},
        {min.x, max.y, max.z},
    };
    AddBoxCorners(corners, color);
}

void LineRenderer::AddBoxCorners(const Vector3 corners[8], const Vector4 &color)
{
    const uint32_t packed = PackLineColor(color);
    // 手前面 / 奥面 / side をつなぐ12辺
    static constexpr int kEdges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // 手前面
        {4, 5}, {5, 6}, {6, 7}, {7, 4}, // 奥面
        {0, 4}, {1, 5}, {2, 6}, {3, 7}, // 側面
    };
    for (const auto &edge : kEdges)
    {
        AddLinePacked(corners[edge[0]], corners[edge[1]], packed);
    }
}

void LineRenderer::AddCube(const Vector3 &center, float size, const Vector4 &color)
{
    const float half = size * 0.5f;
    AddBox({center.x - half, center.y - half, center.z - half},
           {center.x + half, center.y + half, center.z + half},
           color);
}

void LineRenderer::AddCircle(const Vector3 &center, const Vector3 &axisU, const Vector3 &axisV, const Vector4 &color, uint32_t segments)
{
    segments = ClampSegments(segments);
    const UnitCircleTable &circle = GetUnitCircle(segments);
    const uint32_t packed = PackLineColor(color);

    Vector3 prev = center + axisU * circle.cos[0] + axisV * circle.sin[0];
    for (uint32_t i = 1; i <= segments; ++i)
    {
        const Vector3 current = center + axisU * circle.cos[i] + axisV * circle.sin[i];
        AddLinePacked(prev, current, packed);
        prev = current;
    }
}

void LineRenderer::AddSphere(const Vector3 &center, float radius, const Vector4 &color, uint32_t segments)
{
    if (radius <= 0.0f || !IsSphereVisible(center, radius))
    {
        return;
    }

    segments = ClampSegments(segments);
    const UnitCircleTable &circle = GetUnitCircle(segments);
    const uint32_t packed = PackLineColor(color);

    // 直交する3つの大円で球を表す（緯度経度メッシュだと線数がsegments^2に膨らむため）
    const Vector3 axisX = {radius, 0.0f, 0.0f};
    const Vector3 axisY = {0.0f, radius, 0.0f};
    const Vector3 axisZ = {0.0f, 0.0f, radius};

    const Vector3 *planes[3][2] = {
        {&axisX, &axisY}, // XY平面
        {&axisY, &axisZ}, // YZ平面
        {&axisZ, &axisX}, // ZX平面
    };

    for (const auto &plane : planes)
    {
        const Vector3 &u = *plane[0];
        const Vector3 &v = *plane[1];
        Vector3 prev = center + u * circle.cos[0] + v * circle.sin[0];
        for (uint32_t i = 1; i <= segments; ++i)
        {
            const Vector3 current = center + u * circle.cos[i] + v * circle.sin[i];
            AddLinePacked(prev, current, packed);
            prev = current;
        }
    }
}

void LineRenderer::AddCylinder(const Vector3 &center, float radius, float halfHeight, const Vector4 &color, uint32_t segments)
{
    // 円柱を囲む球で早期カリング
    const float boundingRadius = std::sqrt(radius * radius + halfHeight * halfHeight);
    if (!IsSphereVisible(center, boundingRadius))
    {
        return;
    }

    segments = ClampSegments(segments);
    const UnitCircleTable &circle = GetUnitCircle(segments);
    const uint32_t packed = PackLineColor(color);

    const Vector3 topCenter = {center.x, center.y + halfHeight, center.z};
    const Vector3 bottomCenter = {center.x, center.y - halfHeight, center.z};

    // 縦線は4本（0/90/180/270度）だけ引く。輪郭の把握にはこれで足りる
    const uint32_t verticalStride = (std::max)(segments / 4u, 1u);

    Vector3 prevTop = {topCenter.x + radius * circle.cos[0], topCenter.y, topCenter.z + radius * circle.sin[0]};
    Vector3 prevBottom = {bottomCenter.x + radius * circle.cos[0], bottomCenter.y, bottomCenter.z + radius * circle.sin[0]};
    AddLinePacked(prevTop, prevBottom, packed);

    for (uint32_t i = 1; i <= segments; ++i)
    {
        const Vector3 top = {topCenter.x + radius * circle.cos[i], topCenter.y, topCenter.z + radius * circle.sin[i]};
        const Vector3 bottom = {bottomCenter.x + radius * circle.cos[i], bottomCenter.y, bottomCenter.z + radius * circle.sin[i]};
        AddLinePacked(prevTop, top, packed);
        AddLinePacked(prevBottom, bottom, packed);
        if (i % verticalStride == 0 && i != segments)
        {
            AddLinePacked(top, bottom, packed);
        }
        prevTop = top;
        prevBottom = bottom;
    }
}

LineBatchId LineRenderer::CreateBatch(const LineVertex *vertices, uint32_t vertexCount)
{
    if (!vertices || vertexCount < 2)
    {
        return kInvalidLineBatch;
    }

    const LineBatchId id = nextBatchId_++;
    batches_.emplace(id, StaticBatch{});
    UpdateBatch(id, vertices, vertexCount);
    return id;
}

void LineRenderer::UpdateBatch(LineBatchId id, const LineVertex *vertices, uint32_t vertexCount)
{
    auto it = batches_.find(id);
    if (it == batches_.end() || !vertices)
    {
        return;
    }

    // 線分リストなので頂点数は偶数へ丸める
    vertexCount &= ~1u;
    StaticBatch &batch = it->second;

    if (vertexCount == 0)
    {
        batch.vertexCount = 0;
        return;
    }

    // 既存バッファに収まらないときだけ作り直す（GPU使用中の可能性があるので遅延解放）
    if (!batch.buffer || batch.capacityVertices < vertexCount)
    {
        if (batch.buffer)
        {
            pendingReleases_.push_back({batch.buffer, kRingSize});
        }
        batch.buffer = CreateVertexBuffer(vertexCount, batch.view, nullptr);
        batch.capacityVertices = vertexCount;
    }

    // 静的バッチはアップロードヒープ常駐。作成時に一度だけ書き込み、以降GPUが読むだけになる
    LineVertex *mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(batch.buffer->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
    {
        std::memcpy(mapped, vertices, static_cast<size_t>(vertexCount) * sizeof(LineVertex));
        batch.buffer->Unmap(0, nullptr);
    }

    batch.vertexCount = vertexCount;
    batch.view.SizeInBytes = vertexCount * static_cast<UINT>(sizeof(LineVertex));
}

void LineRenderer::DestroyBatch(LineBatchId id)
{
    auto it = batches_.find(id);
    if (it == batches_.end())
    {
        return;
    }
    if (it->second.buffer)
    {
        pendingReleases_.push_back({it->second.buffer, kRingSize});
    }
    batches_.erase(it);
}

void LineRenderer::SubmitBatch(LineBatchId id, const Matrix4x4 &world, const Vector4 &tint)
{
    auto it = batches_.find(id);
    if (it == batches_.end() || it->second.vertexCount == 0)
    {
        return;
    }
    batchSubmissions_.push_back({id, world, tint});
}

bool LineRenderer::IsSphereVisible(const Vector3 &center, float radius) const
{
    if (!frustumValid_)
    {
        return true;
    }
    for (const Vector4 &plane : frustumPlanes_)
    {
        const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
        if (distance < -radius)
        {
            return false;
        }
    }
    return true;
}

void LineRenderer::ExtractFrustum(const Matrix4x4 &viewProjection)
{
    // 行ベクトル規約（clip = pos * M）なので、平面は列の和差で得られる
    auto column = [&viewProjection](int index) -> Vector4 {
        return Vector4(viewProjection.m[0][index], viewProjection.m[1][index],
                       viewProjection.m[2][index], viewProjection.m[3][index]);
    };

    const Vector4 c0 = column(0);
    const Vector4 c1 = column(1);
    const Vector4 c2 = column(2);
    const Vector4 c3 = column(3);

    const Vector4 raw[6] = {
        {c3.x + c0.x, c3.y + c0.y, c3.z + c0.z, c3.w + c0.w}, // 左
        {c3.x - c0.x, c3.y - c0.y, c3.z - c0.z, c3.w - c0.w}, // 右
        {c3.x + c1.x, c3.y + c1.y, c3.z + c1.z, c3.w + c1.w}, // 下
        {c3.x - c1.x, c3.y - c1.y, c3.z - c1.z, c3.w - c1.w}, // 上
        {c2.x, c2.y, c2.z, c2.w},                             // 近
        {c3.x - c2.x, c3.y - c2.y, c3.z - c2.z, c3.w - c2.w}, // 遠
    };

    for (int i = 0; i < 6; ++i)
    {
        const float length = std::sqrt(raw[i].x * raw[i].x + raw[i].y * raw[i].y + raw[i].z * raw[i].z);
        if (length <= 1e-6f)
        {
            // 行列が未初期化などで平面を作れない場合はカリングを無効化する
            frustumValid_ = false;
            return;
        }
        const float inv = 1.0f / length;
        frustumPlanes_[i] = {raw[i].x * inv, raw[i].y * inv, raw[i].z * inv, raw[i].w * inv};
    }
    frustumValid_ = true;
}

Microsoft::WRL::ComPtr<ID3D12Resource> LineRenderer::CreateVertexBuffer(uint32_t vertexCount, D3D12_VERTEX_BUFFER_VIEW &outView, LineVertex **outMapped)
{
    const UINT sizeInBytes = vertexCount * static_cast<UINT>(sizeof(LineVertex));
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer = pDxCommon_->CreateBufferResource(sizeInBytes);

    outView.BufferLocation = buffer->GetGPUVirtualAddress();
    outView.StrideInBytes = sizeof(LineVertex);
    outView.SizeInBytes = sizeInBytes;

    if (outMapped)
    {
        D3D12_RANGE readRange = {0, 0};
        buffer->Map(0, &readRange, reinterpret_cast<void **>(outMapped));
    }
    return buffer;
}

void LineRenderer::EnsureRingCapacity(RingSlot &slot, uint32_t vertexCount)
{
    if (slot.capacityVertices >= vertexCount && slot.buffer)
    {
        return;
    }

    // 頻繁な作り直しを避けるため、必要量の1.5倍かつ最低4096頂点を確保する
    uint32_t newCapacity = (std::max)(vertexCount + vertexCount / 2, 4096u);
    if (slot.buffer)
    {
        // 現在GPUが読んでいる可能性があるので、リング一周ぶん待ってから解放する
        pendingReleases_.push_back({slot.buffer, kRingSize});
        slot.mapped = nullptr;
    }

    slot.buffer = CreateVertexBuffer(newCapacity, slot.view, &slot.mapped);
    slot.capacityVertices = newCapacity;
}

void LineRenderer::TickPendingReleases()
{
    for (size_t i = 0; i < pendingReleases_.size();)
    {
        if (pendingReleases_[i].framesLeft > 0)
        {
            --pendingReleases_[i].framesLeft;
            ++i;
            continue;
        }
        pendingReleases_[i] = std::move(pendingReleases_.back());
        pendingReleases_.pop_back();
    }
}

void LineRenderer::UpdateCameraBuffer(const ViewProjection &viewProjection)
{
    *pCameraData_ = viewProjection.matView_ * viewProjection.matProjection_;
}

void LineRenderer::SetDrawConstants(ID3D12GraphicsCommandList *pCommandList, const Matrix4x4 &world, const Vector4 &tint)
{
    DrawConstants constants{};
    constants.world = world;
    constants.tint = tint;
    // （static 関数なのでインスタンスの pPsoManager_ ではなくシングルトンから取る）
    const ShaderRootSignature *rootSignature =
        PipelineManager::GetInstance()->GetReflectedRootSignature(PipelineType::Line3d);
    assert(rootSignature && "3Dラインのルートシグネチャが未生成です");
    pCommandList->SetGraphicsRoot32BitConstants(rootSignature->GetCbvIndex(1), kDrawConstantsDwords, &constants, 0);
}

void LineRenderer::RecordDrawCommands(ID3D12GraphicsCommandList *pCommandList, D3D12_GPU_VIRTUAL_ADDRESS viewProjCB)
{
    pPsoManager_->DrawCommonSetting(PipelineType::Line3d);
    const ShaderRootSignature *rootSignature = pPsoManager_->GetReflectedRootSignature(PipelineType::Line3d);
    assert(rootSignature && "3Dラインのルートシグネチャが未生成です");
    pCommandList->SetGraphicsRootConstantBufferView(rootSignature->GetCbvIndex(0), viewProjCB);

    // ── 動的線 ──
    if (lineCount_ > 0)
    {
        const uint32_t vertexCount = lineCount_ * 2;
        RingSlot &slot = ring_[ringIndex_];
        EnsureRingCapacity(slot, vertexCount);

        if (slot.mapped)
        {
            // キャッシュ可能メモリからアップロードヒープへ1回のmemcpyで流し込む。
            // 線ごとに書き込む旧実装と違い、ライトコンバイン領域へ連続ストリームで書けるので速い。
            std::memcpy(slot.mapped, staging_.get(), static_cast<size_t>(vertexCount) * sizeof(LineVertex));

            D3D12_VERTEX_BUFFER_VIEW view = slot.view;
            view.SizeInBytes = vertexCount * static_cast<UINT>(sizeof(LineVertex));

            SetDrawConstants(pCommandList, MakeIdentity4x4(), {1.0f, 1.0f, 1.0f, 1.0f});
            pCommandList->IASetVertexBuffers(0, 1, &view);
            pCommandList->DrawInstanced(vertexCount, 1, 0, 0);
        }
    }

    // ── 静的バッチ ──
    for (const BatchSubmission &submission : batchSubmissions_)
    {
        auto it = batches_.find(submission.id);
        if (it == batches_.end() || it->second.vertexCount == 0)
        {
            continue;
        }
        const StaticBatch &batch = it->second;

        SetDrawConstants(pCommandList, submission.world, submission.tint);

        D3D12_VERTEX_BUFFER_VIEW view = batch.view;
        pCommandList->IASetVertexBuffers(0, 1, &view);
        pCommandList->DrawInstanced(batch.vertexCount, 1, 0, 0);
    }
}

void LineRenderer::Render(const ViewProjection &viewProjection)
{
    if (lineCount_ == 0 && batchSubmissions_.empty())
    {
        return;
    }

    UpdateCameraBuffer(viewProjection);
    RecordDrawCommands(pDxCommon_->GetCommandList().Get(), cameraBuffer_->GetGPUVirtualAddress());

    // 次フレームはリングの別スロットを使う（GPUが読んでいる最中のバッファを上書きしない）
    ringIndex_ = (ringIndex_ + 1) % kRingSize;
    lineCount_ = 0;
    batchSubmissions_.clear();
}

void LineRenderer::RenderWithExternalCamera(ID3D12GraphicsCommandList *pCommandList, D3D12_GPU_VIRTUAL_ADDRESS viewProjCB)
{
    if (lineCount_ == 0 && batchSubmissions_.empty())
    {
        return;
    }
    // リセットしない: この後の Render(sceneVP) が同じ線をシーンVPで描画してリセットする。
    // 同じリングスロットへ2回memcpyすることになるが、内容は同一なので問題ない。
    RecordDrawCommands(pCommandList, viewProjCB);
}
} // namespace Hagine
