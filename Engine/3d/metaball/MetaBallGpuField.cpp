#define NOMINMAX
#include "MetaBallGpuField.h"
#include "MarchingCubesTable.h"
#include "DirectXCommon.h"
#include "camera/projection/ViewProjection.h"
#include "graphics/model/ModelManager.h"
#include "graphics/pipeline/ComputePipelineManager.h"
#include "graphics/srv/SrvManager.h"
#include "object/Object3d.h"
#include "render/RenderView.h"
#include "transform/WorldTransform.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Hagine {
namespace {

/// <summary>
/// MetaBall.hlsli の MetaBallConstants と同じ並びの定数
/// </summary>
struct MetaBallConstantsGpu
{
    Vector3 gridOrigin;
    float cellSize;
    uint32_t gridSamples[3];
    uint32_t ballCount;
    float threshold;
    float uvScale;
    uint32_t maxVertexCount;
    float time;
    float wobbleAmplitude;
    float wobbleSpeed;
    float wobbleFrequency;
    float padding;
};

constexpr int kPadCells = 2;                          // 格子の外周に足す余白のセル数
constexpr uint32_t kTriTableElementCount = 256 * 16;  // 三角形テーブルの要素数
constexpr uint32_t kBallStride = 3;                   // 要素1個の float4 の数（METABALL_BALL_STRIDE と同じ）
constexpr uint32_t kSurfaceOutputCount = 3;           // 出力モデルの面数（同時に進むフレーム数 + 1）

/// <summary>
/// UAV への書き込み完了を待つバリア
/// </summary>
void InsertUavBarrier(ID3D12GraphicsCommandList *pCommandList, ID3D12Resource *pResource)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = pResource;
    pCommandList->ResourceBarrier(1, &barrier);
}

/// <summary>
/// 切り上げ除算
/// </summary>
uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
{
    return (value + divisor - 1) / divisor;
}

/// <summary>
/// 要素同士が同じか
/// </summary>
bool IsSameBall(const MetaBallGpuBall &a, const MetaBallGpuBall &b)
{
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.radius == b.radius && a.stiffness == b.stiffness && a.halfExtents.x == b.halfExtents.x &&
           a.halfExtents.y == b.halfExtents.y && a.halfExtents.z == b.halfExtents.z;
}

} // namespace

/// ===================================================
/// MetaBallGpuField
/// ===================================================

MetaBallGpuField::~MetaBallGpuField()
{
    if (ballResource_ && pBallData_)
    {
        ballResource_->Unmap(0, nullptr);
        pBallData_ = nullptr;
    }

    // 記述子を返却（+1 規約のため予約番号で指定）
    if (pSrvManager_)
    {
        if (densityUavIndex_ != 0)
        {
            pSrvManager_->FreeDeferred(densityUavIndex_ - 1);
        }
        if (counterUavIndex_ != 0)
        {
            pSrvManager_->FreeDeferred(counterUavIndex_ - 1);
        }
    }
    ClearTargets();
}

void MetaBallGpuField::Initialize(const std::string &name, uint32_t maxBallCount, uint32_t maxGridSamples,
                                  uint32_t ringSlots)
{
    pDxCommon_ = DirectXCommon::GetInstance();
    pSrvManager_ = SrvManager::GetInstance();
    name_ = name;
    maxBallCount_ = (std::max)(maxBallCount, 1u);
    maxGridSamples_ = std::clamp(maxGridSamples, 8u, 160u);
    ringSlots_ = (std::max)(ringSlots, 1u);

    // 要素配列（毎フレーム CPU が書くので UPLOAD ヒープ。面を分けて順に使用）
    ballResource_ = pDxCommon_->CreateBufferResource(sizeof(Vector4) * kBallStride * maxBallCount_ * ringSlots_);
    ballResource_->SetName(L"MetaBallGpuBalls");
    ballResource_->Map(0, nullptr, reinterpret_cast<void **>(&pBallData_));

    // 密度場
    const uint32_t sampleCount = maxGridSamples_ * maxGridSamples_ * maxGridSamples_;
    densityResource_ = pDxCommon_->CreateBufferResource(sizeof(float) * sampleCount, true);
    densityResource_->SetName(L"MetaBallGpuDensity");
    densityUavIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVStructuredBuffer(densityUavIndex_, densityResource_.Get(), sampleCount, sizeof(float));

    // 頂点数カウンタ
    counterResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * 4, true);
    counterResource_->SetName(L"MetaBallGpuCounter");
    counterUavIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVStructuredBuffer(counterUavIndex_, counterResource_.Get(), 4, sizeof(uint32_t));

    UploadTriTable();
}

void MetaBallGpuField::UploadTriTable()
{
    triTableResource_ = pDxCommon_->CreateBufferResource(sizeof(int32_t) * kTriTableElementCount);
    triTableResource_->SetName(L"MetaBallGpuTriTable");

    int32_t *pData = nullptr;
    triTableResource_->Map(0, nullptr, reinterpret_cast<void **>(&pData));
    for (uint32_t pattern = 0; pattern < 256; ++pattern)
    {
        for (uint32_t slot = 0; slot < 16; ++slot)
        {
            pData[pattern * 16 + slot] = static_cast<int32_t>(kMarchingCubesTriTable[pattern][slot]);
        }
    }
    triTableResource_->Unmap(0, nullptr);
}

Vector4 *MetaBallGpuField::BeginBalls(size_t count)
{
    ballCount_ = (std::min)(static_cast<uint32_t>(count), maxBallCount_);
    maxBallRadius_ = 0.0f;
    constexpr float kInf = (std::numeric_limits<float>::max)();
    boundsMin_ = {kInf, kInf, kInf};
    boundsMax_ = {-kInf, -kInf, -kInf};

    if (ballCount_ == 0 || !pBallData_)
    {
        ballCount_ = 0;
        return nullptr;
    }

    // GPU が読んでいる面を避けて次の面へ書き込む
    currentSlot_ = nextSlot_;
    nextSlot_ = (nextSlot_ + 1) % ringSlots_;
    return pBallData_ + static_cast<size_t>(currentSlot_) * maxBallCount_ * kBallStride;
}

void MetaBallGpuField::WriteBall(Vector4 *pSlot, uint32_t index, const MetaBallGpuBall &ball)
{
    const float radius = (std::max)(ball.radius, 1e-4f);
    const Vector3 extents{std::abs(ball.halfExtents.x), std::abs(ball.halfExtents.y), std::abs(ball.halfExtents.z)};

    // [0] = (中心, 影響半径) / [1] = (密度の高さ, 脈動の位相, 予備, 予備) / [2] = (箱の半分の大きさ, 予備)
    Vector4 *pBall = pSlot + static_cast<size_t>(index) * kBallStride;
    const float phase = ball.position.x + ball.position.y * 1.7f + ball.position.z * 2.3f;
    pBall[0] = Vector4{ball.position.x, ball.position.y, ball.position.z, radius};
    pBall[1] = Vector4{ball.stiffness, phase, 0.0f, 0.0f};
    pBall[2] = Vector4{extents.x, extents.y, extents.z, 0.0f};

    const Vector3 reach{extents.x + radius, extents.y + radius, extents.z + radius};
    boundsMin_.x = (std::min)(boundsMin_.x, ball.position.x - reach.x);
    boundsMin_.y = (std::min)(boundsMin_.y, ball.position.y - reach.y);
    boundsMin_.z = (std::min)(boundsMin_.z, ball.position.z - reach.z);
    boundsMax_.x = (std::max)(boundsMax_.x, ball.position.x + reach.x);
    boundsMax_.y = (std::max)(boundsMax_.y, ball.position.y + reach.y);
    boundsMax_.z = (std::max)(boundsMax_.z, ball.position.z + reach.z);
    maxBallRadius_ = (std::max)(maxBallRadius_, radius);
}

void MetaBallGpuField::SetBalls(const std::vector<Vector3> &positions, float radius, float stiffness)
{
    Vector4 *pSlot = BeginBalls(positions.size());
    if (!pSlot)
    {
        return;
    }
    MetaBallGpuBall ball{};
    ball.radius = radius;
    ball.stiffness = stiffness;
    for (uint32_t i = 0; i < ballCount_; ++i)
    {
        ball.position = positions[i];
        WriteBall(pSlot, i, ball);
    }
}

void MetaBallGpuField::SetBalls(const std::vector<MetaBallGpuBall> &balls)
{
    Vector4 *pSlot = BeginBalls(balls.size());
    if (!pSlot)
    {
        return;
    }
    for (uint32_t i = 0; i < ballCount_; ++i)
    {
        WriteBall(pSlot, i, balls[i]);
    }
}

uint32_t MetaBallGpuField::EnsureTargetUav(ID3D12Resource *pVertexResource, uint32_t vertexCount)
{
    auto it = targetUavIndices_.find(pVertexResource);
    if (it != targetUavIndices_.end())
    {
        return it->second;
    }

    // 頂点は uint の並びとして書き込み（VertexData 1個 = 9 ワード）
    const uint32_t wordCount = vertexCount * 9;
    const uint32_t uavIndex = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVStructuredBuffer(uavIndex, pVertexResource, wordCount, sizeof(uint32_t));
    targetUavIndices_.emplace(pVertexResource, uavIndex);
    return uavIndex;
}

void MetaBallGpuField::ClearTargets()
{
    if (pSrvManager_)
    {
        for (const auto &[resource, uavIndex] : targetUavIndices_)
        {
            pSrvManager_->FreeDeferred(uavIndex - 1);
        }
    }
    targetUavIndices_.clear();
}

void MetaBallGpuField::Dispatch(ID3D12GraphicsCommandList *pCommandList, Object3d *target,
                                const MetaBallGpuParams &params, float time)
{
    if (!pCommandList || !target || !pDxCommon_)
    {
        return;
    }
    ID3D12Resource *pVertexResource = target->GetGpuVertexResource();
    const uint32_t maxVertexCount = target->GetGpuVertexCapacity();
    if (!pVertexResource || maxVertexCount == 0)
    {
        return;
    }

    const uint32_t targetUavIndex = EnsureTargetUav(pVertexResource, maxVertexCount);

    // 格子の範囲（脈動で膨らむ分を含める）
    const float wobbleMargin = maxBallRadius_ * (std::max)(params.wobbleAmplitude, 0.0f);
    const Vector3 margin{wobbleMargin, wobbleMargin, wobbleMargin};
    const Vector3 boundsMin = ballCount_ > 0 ? boundsMin_ - margin : Vector3{};
    const Vector3 boundsMax = ballCount_ > 0 ? boundsMax_ + margin : Vector3{};
    const Vector3 extent = boundsMax - boundsMin;

    // 格子が上限を超える場合はセルを粗くして収める
    const float longestEdge = (std::max)({extent.x, extent.y, extent.z, 1e-4f});
    const float minimumCellSize = longestEdge / static_cast<float>(maxGridSamples_ - 1 - kPadCells * 2);
    const float cellSize = (std::max)({params.voxelSize, minimumCellSize, 1e-4f});

    auto axisSampleCount = [&](float length) {
        const int count = static_cast<int>(std::ceil(length / cellSize)) + 1 + kPadCells * 2;
        return static_cast<uint32_t>(std::clamp(count, 2, static_cast<int>(maxGridSamples_)));
    };
    const uint32_t gridX = axisSampleCount(extent.x);
    const uint32_t gridY = axisSampleCount(extent.y);
    const uint32_t gridZ = axisSampleCount(extent.z);
    const Vector3 origin = boundsMin - Vector3{cellSize, cellSize, cellSize} * static_cast<float>(kPadCells);

    stats_.ballCount = ballCount_;
    stats_.gridX = gridX;
    stats_.gridY = gridY;
    stats_.gridZ = gridZ;
    stats_.cellCount = (gridX - 1) * (gridY - 1) * (gridZ - 1);
    stats_.maxVertexCount = maxVertexCount;
    stats_.cellSize = cellSize;

    MetaBallConstantsGpu constants{};
    constants.gridOrigin = origin;
    constants.cellSize = cellSize;
    constants.gridSamples[0] = gridX;
    constants.gridSamples[1] = gridY;
    constants.gridSamples[2] = gridZ;
    constants.ballCount = ballCount_;
    constants.threshold = params.threshold;
    constants.uvScale = params.uvScale;
    constants.maxVertexCount = maxVertexCount;
    constants.time = time;
    constants.wobbleAmplitude = params.wobbleAmplitude;
    constants.wobbleSpeed = params.wobbleSpeed;
    constants.wobbleFrequency = params.wobbleFrequency;
    static_assert(sizeof(MetaBallConstantsGpu) == sizeof(uint32_t) * 16,
                  "MetaBall.hlsli の MetaBallConstants とルートシグネチャの定数の数を合わせること");
    constexpr UINT kConstantCount = sizeof(MetaBallConstantsGpu) / sizeof(uint32_t);

    ComputePipelineManager *pipelineManager = ComputePipelineManager::GetInstance();
    const D3D12_GPU_VIRTUAL_ADDRESS ballAddress =
        ballResource_->GetGPUVirtualAddress() +
        static_cast<UINT64>(currentSlot_) * maxBallCount_ * kBallStride * sizeof(Vector4);

    // 出力を面積0に潰してカウンタを初期化（要素が0個でも実行）
    pipelineManager->DrawCommonSetting(ComputePipelineType::MetaBallClear, BlendMode::Normal,
                                       ShaderMode::None, pCommandList);
    pCommandList->SetComputeRoot32BitConstants(0, kConstantCount, &constants, 0);
    pCommandList->SetComputeRootDescriptorTable(1, pSrvManager_->GetGPUDescriptorHandle(targetUavIndex));
    pCommandList->SetComputeRootDescriptorTable(2, pSrvManager_->GetGPUDescriptorHandle(counterUavIndex_));
    pCommandList->Dispatch(DivideRoundUp(maxVertexCount, 64), 1, 1);

    if (ballCount_ == 0)
    {
        return;
    }

    // 密度場
    pipelineManager->DrawCommonSetting(ComputePipelineType::MetaBallDensity, BlendMode::Normal,
                                       ShaderMode::None, pCommandList);
    pCommandList->SetComputeRoot32BitConstants(0, kConstantCount, &constants, 0);
    pCommandList->SetComputeRootShaderResourceView(1, ballAddress);
    pCommandList->SetComputeRootDescriptorTable(2, pSrvManager_->GetGPUDescriptorHandle(densityUavIndex_));
    pCommandList->Dispatch(DivideRoundUp(gridX, 4), DivideRoundUp(gridY, 4), DivideRoundUp(gridZ, 4));

    InsertUavBarrier(pCommandList, densityResource_.Get());
    InsertUavBarrier(pCommandList, counterResource_.Get());
    InsertUavBarrier(pCommandList, pVertexResource);

    // マーチングキューブス
    pipelineManager->DrawCommonSetting(ComputePipelineType::MetaBallMarch, BlendMode::Normal,
                                       ShaderMode::None, pCommandList);
    pCommandList->SetComputeRoot32BitConstants(0, kConstantCount, &constants, 0);
    pCommandList->SetComputeRootShaderResourceView(1, triTableResource_->GetGPUVirtualAddress());
    pCommandList->SetComputeRootDescriptorTable(2, pSrvManager_->GetGPUDescriptorHandle(densityUavIndex_));
    pCommandList->SetComputeRootDescriptorTable(3, pSrvManager_->GetGPUDescriptorHandle(targetUavIndex));
    pCommandList->SetComputeRootDescriptorTable(4, pSrvManager_->GetGPUDescriptorHandle(counterUavIndex_));
    pCommandList->Dispatch(DivideRoundUp(gridX - 1, 4), DivideRoundUp(gridY - 1, 4), DivideRoundUp(gridZ - 1, 4));

    // 書き込み完了を待つ（次の Dispatch で密度場・カウンタを使い回すため）
    InsertUavBarrier(pCommandList, densityResource_.Get());
    InsertUavBarrier(pCommandList, counterResource_.Get());
    InsertUavBarrier(pCommandList, pVertexResource);
}

/// ===================================================
/// MetaBallGpuSurface
/// ===================================================

MetaBallGpuSurface::MetaBallGpuSurface() = default;

MetaBallGpuSurface::~MetaBallGpuSurface()
{
    // モデルの実体は ModelManager の所有なので登録も解除
    for (std::unique_ptr<Object3d> &output : outputs_)
    {
        const std::string key = output->GetDynamicModelKey();
        output.reset();
        if (!key.empty())
        {
            ModelManager::GetInstance()->RemoveModel(key);
        }
    }
    field_.ClearTargets();
}

void MetaBallGpuSurface::Initialize(const std::string &name, const std::string &texturePath, uint32_t maxBallCount,
                                    uint32_t maxTriangleCount, uint32_t maxGridSamples)
{
    field_.Initialize(name, maxBallCount, maxGridSamples);

    const uint32_t maxVertexCount = (std::max)(maxTriangleCount, 1u) * 3;
    for (uint32_t i = 0; i < kSurfaceOutputCount; ++i)
    {
        std::unique_ptr<Object3d> output = std::make_unique<Object3d>();
        output->Initialize();
        output->CreateGpuWritableModel(texturePath, maxVertexCount);
        output->SetBlendMode(BlendMode::Normal);
        outputs_.push_back(std::move(output));
    }

    transform_ = std::make_unique<WorldTransform>();
    transform_->Initialize();
    transform_->UpdateMatrix();
}

void MetaBallGpuSurface::SetBalls(const std::vector<MetaBallGpuBall> &balls)
{
    balls_ = balls;
}

bool MetaBallGpuSurface::HasChanged(const MetaBallGpuParams &params) const
{
    if (drawIndex_ < 0 || params.wobbleAmplitude > 0.0f)
    {
        return true;
    }
    const MetaBallGpuParams &last = dispatchedParams_;
    if (params.voxelSize != last.voxelSize || params.threshold != last.threshold || params.uvScale != last.uvScale ||
        params.wobbleAmplitude != last.wobbleAmplitude)
    {
        return true;
    }
    return !std::equal(balls_.begin(), balls_.end(), dispatchedBalls_.begin(), dispatchedBalls_.end(), IsSameBall);
}

void MetaBallGpuSurface::Dispatch(const MetaBallGpuParams &params, float time)
{
    if (outputs_.empty() || !HasChanged(params))
    {
        return;
    }

    DirectXCommon *pDxCommon = DirectXCommon::GetInstance();
    ID3D12GraphicsCommandList *pCommandList = pDxCommon->GetComputeCommandList().Get();
    if (!pCommandList)
    {
        return;
    }
    pDxCommon->BeginComputeFrame();

    // 前のフレームが描画中の面を避けて次の面へ書き込む
    drawIndex_ = (drawIndex_ + 1) % static_cast<int>(outputs_.size());
    field_.SetBalls(balls_);
    field_.Dispatch(pCommandList, outputs_[drawIndex_].get(), params, time);

    dispatchedBalls_ = balls_;
    dispatchedParams_ = params;
    hasMesh_ = !balls_.empty();
}

void MetaBallGpuSurface::Draw(const ViewProjection &viewProjection, bool lighting)
{
    // カメラビュー窓では描かない（定数バッファがメインの1組のみ）
    if (!hasMesh_ || drawIndex_ < 0 || RenderView::IsExtra())
    {
        return;
    }
    outputs_[drawIndex_]->Draw(*transform_, viewProjection, false, lighting, true);
}

void MetaBallGpuSurface::SetColor(const Vector4 &color)
{
    for (std::unique_ptr<Object3d> &output : outputs_)
    {
        output->SetColor(color);
    }
}

void MetaBallGpuSurface::SetUseDeferred(bool use)
{
    for (std::unique_ptr<Object3d> &output : outputs_)
    {
        output->SetUseDeferred(use);
    }
}

} // namespace Hagine
