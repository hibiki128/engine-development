#define NOMINMAX
#include "MetaBallGpuField.h"
#include "MarchingCubesTable.h"
#include "DirectXCommon.h"
#include "graphics/pipeline/ComputePipelineManager.h"
#include "graphics/srv/SrvManager.h"
#include "object/Object3d.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Hagine {
namespace {

/// MetaBall.hlsli の MetaBallConstants と同じ並び（16 バイト境界がそろうように詰めてある）
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

/// 外周をこのセル数ぶん余白にする。一番外のサンプル点が必ず密度 0 になり、表面が閉じる
constexpr int kPadCells = 2;
/// 三角形テーブルの要素数（256 パターン × 16）
constexpr uint32_t kTriTableElementCount = 256 * 16;

/// <summary>UAV への書き込み完了を待つバリア</summary>
void InsertUavBarrier(ID3D12GraphicsCommandList *pCommandList, ID3D12Resource *pResource)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = pResource;
    pCommandList->ResourceBarrier(1, &barrier);
}

/// <summary>切り上げ除算（ディスパッチするグループ数の計算に使う）</summary>
uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
{
    return (value + divisor - 1) / divisor;
}

} // namespace

MetaBallGpuField::~MetaBallGpuField()
{
    if (ballResource_ && pBallData_)
    {
        ballResource_->Unmap(0, nullptr);
        pBallData_ = nullptr;
    }
}

void MetaBallGpuField::Initialize(const std::string &name, uint32_t maxBallCount, uint32_t maxGridSamples,
                                  uint32_t ringSlots)
{
    pDxCommon_ = DirectXCommon::GetInstance();
    pSrvManager_ = SrvManager::GetInstance();
    name_ = name;
    maxBallCount_ = (std::max)(maxBallCount, 1u);
    // 1 軸あたりのサンプル点数。3 乗で効くので上限を設けておく
    maxGridSamples_ = std::clamp(maxGridSamples, 8u, 160u);
    ringSlots_ = (std::max)(ringSlots, 1u);

    // ---- 入力: ボール配列（毎フレーム CPU が書くので UPLOAD ヒープ）----
    // 1 フレームに何度も Dispatch するので、面を分けて順ぐりに使う。
    // 1 面しか無いと、GPU がまだ読んでいない前の色のぶんを上書きしてしまう
    ballResource_ = pDxCommon_->CreateBufferResource(sizeof(Vector4) * 2 * maxBallCount_ * ringSlots_);
    ballResource_->SetName(L"MetaBallGpuBalls");
    ballResource_->Map(0, nullptr, reinterpret_cast<void **>(&pBallData_));

    // ---- 中間: 密度場 ----
    const uint32_t sampleCount = maxGridSamples_ * maxGridSamples_ * maxGridSamples_;
    densityResource_ = pDxCommon_->CreateBufferResource(sizeof(float) * sampleCount, true);
    densityResource_->SetName(L"MetaBallGpuDensity");
    // SrvManager は「Allocate() が返した番号 r に対し、実際に書き込むのは r+1」という規約で
    // 運用されている（エンジン内の他の確保箇所はすべて + 1 している）。
    // ここだけ素の r に書いていたため、r に書き込み済みだった別のリソースの
    // ディスクリプタを潰していた。シーンを作り直すと ImGui のフォントアトラスが
    // ちょうどこの枠に当たり、GUI が丸ごと見えなくなる不具合になっていた。
    densityUavIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVStructuredBuffer(densityUavIndex_, densityResource_.Get(), sampleCount, sizeof(float));

    // ---- 出力: 頂点数カウンタ ----
    counterResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * 4, true);
    counterResource_->SetName(L"MetaBallGpuCounter");
    counterUavIndex_ = pSrvManager_->Allocate() + 1; // +1 規約（上のコメント参照）
    pSrvManager_->CreateUAVStructuredBuffer(counterUavIndex_, counterResource_.Get(), 4, sizeof(uint32_t));

    // 定数はルート定数で渡すので、バッファは持たない
    //（1フレームに何色ぶんも積むため、共用の定数バッファでは最後の色の値に上書きされてしまう）

    UploadTriTable();
}

void MetaBallGpuField::UploadTriTable()
{
    // 三角形テーブルは中身が変わらないので、作るときに一度書いて以後は触らない。
    // CPU 版（MetaBallBuilder）と同じテーブルを使うので、出てくる形も揃う
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

void MetaBallGpuField::SetBalls(const std::vector<Vector3> &positions, float radius, float stiffness)
{
    ballCount_ = (std::min)(static_cast<uint32_t>(positions.size()), maxBallCount_);
    ballRadius_ = (std::max)(radius, 1e-4f);
    ballStiffness_ = stiffness;

    if (ballCount_ == 0 || !pBallData_)
    {
        return;
    }

    // 今回書き込む面を決める。GPU がまだ前の面を読んでいても踏まないよう順ぐりに使う
    currentSlot_ = nextSlot_;
    nextSlot_ = (nextSlot_ + 1) % ringSlots_;
    Vector4 *pSlot = pBallData_ + static_cast<size_t>(currentSlot_) * maxBallCount_ * 2;

    boundsMin_ = positions[0];
    boundsMax_ = positions[0];

    for (uint32_t i = 0; i < ballCount_; ++i)
    {
        const Vector3 &position = positions[i];

        // [i*2+0] = (中心, 影響半径) / [i*2+1] = (密度の高さ, 脈動の位相, 予備, 予備)
        pSlot[i * 2 + 0] = Vector4{position.x, position.y, position.z, ballRadius_};
        // 位相は位置から作る。ボールごとにずれるので、殻全体がうねって見える
        const float phase = position.x + position.y * 1.7f + position.z * 2.3f;
        pSlot[i * 2 + 1] = Vector4{ballStiffness_, phase, 0.0f, 0.0f};

        boundsMin_.x = (std::min)(boundsMin_.x, position.x);
        boundsMin_.y = (std::min)(boundsMin_.y, position.y);
        boundsMin_.z = (std::min)(boundsMin_.z, position.z);
        boundsMax_.x = (std::max)(boundsMax_.x, position.x);
        boundsMax_.y = (std::max)(boundsMax_.y, position.y);
        boundsMax_.z = (std::max)(boundsMax_.z, position.z);
    }

    // 影響半径のぶんだけ広げる（脈動で膨らむぶんも後で足す）
    const Vector3 margin{ballRadius_, ballRadius_, ballRadius_};
    boundsMin_ = boundsMin_ - margin;
    boundsMax_ = boundsMax_ + margin;
}

uint32_t MetaBallGpuField::EnsureTargetUav(ID3D12Resource *pVertexResource, uint32_t vertexCount)
{
    auto it = targetUavIndices_.find(pVertexResource);
    if (it != targetUavIndices_.end())
    {
        return it->second;
    }

    // 頂点は uint の並びとして書く（VertexData 1 個 = 9 ワード）。
    // HLSL と C++ で構造体の詰め方が食い違わないよう、あえて素の uint 配列として扱う
    const uint32_t wordCount = vertexCount * 9;
    const uint32_t uavIndex = pSrvManager_->Allocate() + 1; // +1 規約（Initialize のコメント参照）
    pSrvManager_->CreateUAVStructuredBuffer(uavIndex, pVertexResource, wordCount, sizeof(uint32_t));
    targetUavIndices_.emplace(pVertexResource, uavIndex);
    return uavIndex;
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
        return; // CreateGpuWritableModel() で作ったモデルでなければ何もできない
    }

    const uint32_t targetUavIndex = EnsureTargetUav(pVertexResource, maxVertexCount);

    // ---- 格子を決める --------------------------------------------------
    // 脈動で膨らむぶんも入れて、表面が箱から出ないようにする
    const float wobbleMargin = ballRadius_ * (std::max)(params.wobbleAmplitude, 0.0f);
    const Vector3 margin{wobbleMargin, wobbleMargin, wobbleMargin};
    const Vector3 boundsMin = boundsMin_ - margin;
    const Vector3 boundsMax = boundsMax_ + margin;
    const Vector3 extent = boundsMax - boundsMin;

    // セルを細かくしすぎて格子が上限を超える場合は、切り捨てずにセルを粗くして収める
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

    // ---- 定数を書く ----------------------------------------------------
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
    // 定数はルート定数として、この Dispatch のコマンドに直接焼き込む
    static_assert(sizeof(MetaBallConstantsGpu) == sizeof(uint32_t) * 16,
                  "MetaBall.hlsli の MetaBallConstants とルートシグネチャの定数の数を合わせること");
    constexpr UINT kConstantCount = sizeof(MetaBallConstantsGpu) / sizeof(uint32_t);

    ComputePipelineManager *pipelineManager = ComputePipelineManager::GetInstance();
    // ボール配列は今回書いた面の先頭を渡す
    const D3D12_GPU_VIRTUAL_ADDRESS ballAddress =
        ballResource_->GetGPUVirtualAddress() +
        static_cast<UINT64>(currentSlot_) * maxBallCount_ * 2 * sizeof(Vector4);

    // ---- 1. 出力を面積0に潰し、カウンタを戻す --------------------------
    // ボールが 0 個でもここは必ず走らせる。走らせないと前フレームの殻が残ってしまう
    pipelineManager->DrawCommonSetting(ComputePipelineType::MetaBallClear, BlendMode::Normal,
                                       ShaderMode::None, pCommandList);
    pCommandList->SetComputeRoot32BitConstants(0, kConstantCount, &constants, 0);
    pCommandList->SetComputeRootDescriptorTable(1, pSrvManager_->GetGPUDescriptorHandle(targetUavIndex));
    pCommandList->SetComputeRootDescriptorTable(2, pSrvManager_->GetGPUDescriptorHandle(counterUavIndex_));
    pCommandList->Dispatch(DivideRoundUp(maxVertexCount, 64), 1, 1);

    if (ballCount_ == 0)
    {
        return; // 消え切った色。潰しただけで終わる
    }

    // ---- 2. 密度場 -----------------------------------------------------
    pipelineManager->DrawCommonSetting(ComputePipelineType::MetaBallDensity, BlendMode::Normal,
                                       ShaderMode::None, pCommandList);
    pCommandList->SetComputeRoot32BitConstants(0, kConstantCount, &constants, 0);
    pCommandList->SetComputeRootShaderResourceView(1, ballAddress);
    pCommandList->SetComputeRootDescriptorTable(2, pSrvManager_->GetGPUDescriptorHandle(densityUavIndex_));
    pCommandList->Dispatch(DivideRoundUp(gridX, 4), DivideRoundUp(gridY, 4), DivideRoundUp(gridZ, 4));

    // 密度場の書き込みと、1. の潰し・カウンタ初期化が終わってから 3. を始める
    InsertUavBarrier(pCommandList, densityResource_.Get());
    InsertUavBarrier(pCommandList, counterResource_.Get());
    InsertUavBarrier(pCommandList, pVertexResource);

    // ---- 3. マーチングキューブス ---------------------------------------
    pipelineManager->DrawCommonSetting(ComputePipelineType::MetaBallMarch, BlendMode::Normal,
                                       ShaderMode::None, pCommandList);
    pCommandList->SetComputeRoot32BitConstants(0, kConstantCount, &constants, 0);
    pCommandList->SetComputeRootShaderResourceView(1, triTableResource_->GetGPUVirtualAddress());
    pCommandList->SetComputeRootDescriptorTable(2, pSrvManager_->GetGPUDescriptorHandle(densityUavIndex_));
    pCommandList->SetComputeRootDescriptorTable(3, pSrvManager_->GetGPUDescriptorHandle(targetUavIndex));
    pCommandList->SetComputeRootDescriptorTable(4, pSrvManager_->GetGPUDescriptorHandle(counterUavIndex_));
    pCommandList->Dispatch(DivideRoundUp(gridX - 1, 4), DivideRoundUp(gridY - 1, 4), DivideRoundUp(gridZ - 1, 4));

    // 次の色が同じ密度場・カウンタを使い回すので、ここで書き込みを締める
    InsertUavBarrier(pCommandList, densityResource_.Get());
    InsertUavBarrier(pCommandList, counterResource_.Get());
    InsertUavBarrier(pCommandList, pVertexResource);
}

} // namespace Hagine
