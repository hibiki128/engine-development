#include "RaytracingScene.h"
#include <DirectXCommon.h>
#include <algorithm>
#include <animation/Skin.h>
#include <cassert>
#include <cstring>
#include <debug/log/Logger.h>
#include <format>
#include <graphics/srv/SrvManager.h>
#include <iterator>
#include <model/Model.h>
#include <model/ModelStructs.h>
#include <model/mesh/Mesh.h>

namespace Hagine {
namespace {

/// 加速構造はアライメント要件があるので、要求サイズを切り上げる
UINT64 AlignTo(UINT64 value, UINT64 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/// 積まれなくなったBLASを何フレーム持っておくか。
/// シーンを作り直すとモデルごと消えるので、置きっぱなしにせずここで捨てる。
/// 描画は数フレーム先行しているため、GPUが使い終わるより短くすると解放で落ちる
constexpr uint64_t kBlasKeepFrames = 8;
} // namespace

void RaytracingScene::Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager)
{
    pDxCommon_ = pDxCommon;
    pSrvManager_ = pSrvManager;

    available_ = pDxCommon_->IsRaytracingSupported() &&
                 pDxCommon_->GetDevice5() != nullptr &&
                 pDxCommon_->GetCommandList4() != nullptr;

    if (!available_)
    {
        Logger::Log("RaytracingScene: この環境ではレイトレーシングを使いません\n");
        return;
    }

    Logger::Log("RaytracingScene: 初期化しました\n");
}

void RaytracingScene::Finalize()
{
    blasCache_.clear();
    skinnedBlasCache_.clear();
    submissions_.clear();
    instances_.clear();
    tlasResult_.Reset();
    tlasScratch_.Reset();
    instanceBuffer_.Reset();
    instanceBufferCapacity_ = 0;
    tlasBuilt_ = false;
    tlasAttemptedThisFrame_ = false;
    active_ = false;
    useRequested_ = false;
    skinnedBuiltThisFrame_ = 0;
}

Microsoft::WRL::ComPtr<ID3D12Resource> RaytracingScene::CreateBuffer(UINT64 sizeInBytes,
                                                                     D3D12_RESOURCE_STATES initialState)
{
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = (std::max)(sizeInBytes, static_cast<UINT64>(1));
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    // 加速構造も作業領域も UAV として書かれる
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    HRESULT hr = pDxCommon_->GetDevice()->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr) && "加速構造用バッファの作成に失敗");
    return resource;
}

D3D12_GPU_VIRTUAL_ADDRESS RaytracingScene::AcquireBlas(Model *pModel)
{
    auto it = blasCache_.find(pModel);
    if (it != blasCache_.end())
    {
        it->second.lastUsedFrame = frameIndex_;
        return it->second.result ? it->second.result->GetGPUVirtualAddress() : 0;
    }

    // 形が取れないモデルは「BLASなし」として覚えておく（毎フレーム作り直そうとしないため）
    auto rememberEmpty = [this, pModel]() {
        Blas empty;
        empty.lastUsedFrame = frameIndex_;
        blasCache_.emplace(pModel, std::move(empty));
        return static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(0);
    };

    // ── ジオメトリ記述を組む ──
    // 頂点は VertexData（position=float4 が先頭）。DXR は先頭3成分だけ見るので、
    // ストライドさえ合っていれば float4 のままで問題ない
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
    const size_t meshCount = pModel->GetMeshCount();
    geometries.reserve(meshCount);

    for (uint32_t i = 0; i < meshCount; ++i)
    {
        Mesh *pMesh = pModel->GetMesh(i);
        if (!pMesh || pMesh->GetIndexCount() == 0 || pMesh->GetVertexCount() == 0)
        {
            continue;
        }
        // GPUが書き換えるメッシュ（メタボールの表面）は、毎フレーム作り直さないと
        // 形がずれる。今のところ対象外にしておく
        if (pMesh->IsGpuWritable())
        {
            continue;
        }

        const D3D12_VERTEX_BUFFER_VIEW vbv = pMesh->GetVertexBufferView();
        const D3D12_INDEX_BUFFER_VIEW ibv = pMesh->GetIndexBufferView();

        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        // 影を落とすだけなので、貫通するかどうかの判定（any-hit）は要らない
        geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexBuffer.StartAddress = vbv.BufferLocation;
        geometry.Triangles.VertexBuffer.StrideInBytes = vbv.StrideInBytes;
        geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.VertexCount = pMesh->GetVertexCount();
        geometry.Triangles.IndexBuffer = ibv.BufferLocation;
        geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geometry.Triangles.IndexCount = pMesh->GetIndexCount();
        geometry.Triangles.Transform3x4 = 0; // 位置合わせは TLAS 側のインスタンス行列で行う
        geometries.push_back(geometry);
    }

    if (geometries.empty())
    {
        return rememberEmpty();
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = static_cast<UINT>(geometries.size());
    inputs.pGeometryDescs = geometries.data();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
    pDxCommon_->GetDevice5()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    if (prebuild.ResultDataMaxSizeInBytes == 0)
    {
        return rememberEmpty();
    }

    Blas blas;
    blas.scratch = CreateBuffer(AlignTo(prebuild.ScratchDataSizeInBytes,
                                        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    blas.result = CreateBuffer(AlignTo(prebuild.ResultDataMaxSizeInBytes,
                                       D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    blas.lastUsedFrame = frameIndex_;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = blas.scratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = blas.result->GetGPUVirtualAddress();

    ID3D12GraphicsCommandList4 *pCommandList = pDxCommon_->GetCommandList4();
    pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    // 構築が終わるまで、この BLAS を参照する TLAS 構築を始めさせない
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = blas.result.Get();
    pCommandList->ResourceBarrier(1, &barrier);

    const D3D12_GPU_VIRTUAL_ADDRESS address = blas.result->GetGPUVirtualAddress();
    blasCache_.emplace(pModel, std::move(blas));
    return address;
}

D3D12_GPU_VIRTUAL_ADDRESS RaytracingScene::AcquireSkinnedBlas(Model *pModel)
{
    Skin *pSkin = pModel->GetSkin();
    if (!pSkin)
    {
        return 0;
    }
    ID3D12Resource *pVertexResource = pSkin->GetOutputVertexResource();
    if (!pVertexResource)
    {
        return 0;
    }

    SkinnedBlas &entry = skinnedBlasCache_[pModel];
    entry.lastUsedFrame = frameIndex_;
    entry.pVertexResource = pVertexResource;

    // ── ジオメトリ記述を組む ──
    // 入力はスキニング結果のバッファ。アニメーションを切り替えると Skin ごと
    // 差し替わってアドレスも変わるので、記述は毎フレーム作り直す。
    // 全メッシュぶんが1本のバッファに連なっているので、描画の BaseVertexLocation と
    // 同じオフセットでメッシュごとの先頭をずらす
    const D3D12_GPU_VIRTUAL_ADDRESS vertexAddress = pVertexResource->GetGPUVirtualAddress();
    entry.geometries.clear();
    const size_t meshCount = pModel->GetMeshCount();
    entry.geometries.reserve(meshCount);

    for (uint32_t i = 0; i < meshCount; ++i)
    {
        Mesh *pMesh = pModel->GetMesh(i);
        if (!pMesh || pMesh->GetIndexCount() == 0 || pMesh->GetVertexCount() == 0)
        {
            continue;
        }

        const D3D12_INDEX_BUFFER_VIEW ibv = pMesh->GetIndexBufferView();

        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexBuffer.StartAddress =
            vertexAddress + static_cast<UINT64>(pSkin->GetMeshVertexOffset(i)) * sizeof(VertexData);
        geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(VertexData);
        geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.VertexCount = pMesh->GetVertexCount();
        geometry.Triangles.IndexBuffer = ibv.BufferLocation;
        geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geometry.Triangles.IndexCount = pMesh->GetIndexCount();
        geometry.Triangles.Transform3x4 = 0;
        entry.geometries.push_back(geometry);
    }

    if (entry.geometries.empty())
    {
        return 0;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    // 毎フレーム作り直すので、探索の速さより構築の速さを優先する
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.NumDescs = static_cast<UINT>(entry.geometries.size());
    inputs.pGeometryDescs = entry.geometries.data();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
    pDxCommon_->GetDevice5()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    if (prebuild.ResultDataMaxSizeInBytes == 0)
    {
        return 0;
    }

    const UINT64 scratchSize = AlignTo(prebuild.ScratchDataSizeInBytes,
                                       D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    const UINT64 resultSize = AlignTo(prebuild.ResultDataMaxSizeInBytes,
                                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    if (!entry.scratch || entry.scratch->GetDesc().Width < scratchSize)
    {
        entry.scratch = CreateBuffer(scratchSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (!entry.result || entry.result->GetDesc().Width < resultSize)
    {
        entry.result = CreateBuffer(resultSize, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        entry.builtOnce = false; // 器を作り直したので中身はまだ無い
    }

    entry.needsBuild = true;
    return entry.result->GetGPUVirtualAddress();
}

bool RaytracingScene::BuildSkinnedBlas(Model *pModel)
{
    if (!IsActive() || !pModel)
    {
        return false;
    }

    auto it = skinnedBlasCache_.find(pModel);
    if (it == skinnedBlasCache_.end())
    {
        return false;
    }

    // このフレームに積まれていない（＝Submit を通っていない）ものは作らない。
    // 記述が前フレームのままなので、作り直しても意味が無い
    SkinnedBlas &entry = it->second;
    if (!entry.needsBuild || !entry.result || !entry.scratch || !entry.pVertexResource ||
        entry.geometries.empty())
    {
        return false;
    }

    ID3D12GraphicsCommandList4 *pCommandList = pDxCommon_->GetCommandList4();
    if (!pCommandList)
    {
        return false;
    }

    // スキニングの書き込み（UAV）が終わってから読む。
    // 加速構造の入力頂点バッファは NON_PIXEL_SHADER_RESOURCE でなければならない
    D3D12_RESOURCE_BARRIER toRead{};
    toRead.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRead.Transition.pResource = entry.pVertexResource;
    toRead.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRead.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toRead.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    pCommandList->ResourceBarrier(1, &toRead);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.NumDescs = static_cast<UINT>(entry.geometries.size());
    inputs.pGeometryDescs = entry.geometries.data();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = entry.scratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = entry.result->GetGPUVirtualAddress();
    pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    // 構築が終わるまで、この BLAS を参照する TLAS 構築を始めさせない
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = entry.result.Get();
    pCommandList->ResourceBarrier(1, &barrier);

    entry.needsBuild = false;
    entry.builtOnce = true;
    ++skinnedBuiltThisFrame_;
    return true;
}

void RaytracingScene::EvictUnusedBlas()
{
    if (frameIndex_ <= kBlasKeepFrames)
    {
        return;
    }
    const uint64_t threshold = frameIndex_ - kBlasKeepFrames;

    for (auto it = blasCache_.begin(); it != blasCache_.end();)
    {
        it = (it->second.lastUsedFrame < threshold) ? blasCache_.erase(it) : std::next(it);
    }
    for (auto it = skinnedBlasCache_.begin(); it != skinnedBlasCache_.end();)
    {
        it = (it->second.lastUsedFrame < threshold) ? skinnedBlasCache_.erase(it) : std::next(it);
    }
}

void RaytracingScene::OnModelDestroyed(const Model *pModel)
{
    // const を外して引くだけ（キーとしてのアドレス比較しかしない）
    Model *pKey = const_cast<Model *>(pModel);
    blasCache_.erase(pKey);
    skinnedBlasCache_.erase(pKey);

    // このフレームに積まれていたら、それも取り下げる。
    // 捨てたモデルをTLASへ載せると、解放済みのBLASのアドレスを指すことになる
    std::erase_if(submissions_, [pKey](const Submission &submission) { return submission.pModel == pKey; });
}

void RaytracingScene::RequestUse()
{
    useRequested_ = true;
}

void RaytracingScene::BeginFrame()
{
    // 前のフレームに要求があったときだけ、このフレームは加速構造を作る。
    // 使う側が動くのは描画フェーズで、積むのはその前なので1フレーム遅れになる
    active_ = useRequested_;
    useRequested_ = false;

    ++frameIndex_;
    submissions_.clear();
    instances_.clear();
    tlasBuilt_ = false;
    tlasAttemptedThisFrame_ = false;
    skinnedBuiltThisFrame_ = 0;

    // シーンを作り直すとモデルごと消えるので、使われなくなったBLASを捨てる
    EvictUnusedBlas();
}

void RaytracingScene::Submit(Model *pModel, const Matrix4x4 &worldMatrix)
{
    if (!IsActive() || !pModel)
    {
        return;
    }

    // スキニングで動くモデルは形が毎フレーム変わるので、使い回しのBLASには載せられない。
    // ここでは器だけ用意しておき、スキニングの直後に BuildSkinnedBlas が中身を作る
    const bool skinned = pModel->IsSkinned();
    const D3D12_GPU_VIRTUAL_ADDRESS blasAddress =
        skinned ? AcquireSkinnedBlas(pModel) : AcquireBlas(pModel);
    if (blasAddress == 0)
    {
        return;
    }

    Submission submission;
    submission.pModel = pModel;
    submission.skinned = skinned;

    // DXR の Transform は「列ベクトル規約の 3x4」。
    // このエンジンは行ベクトル規約（world = pos * M）なので、
    // 3x3 部分は転置し、平行移動は M の4行目から持ってくる
    for (int row = 0; row < 3; ++row)
    {
        submission.desc.Transform[row][0] = worldMatrix.m[0][row];
        submission.desc.Transform[row][1] = worldMatrix.m[1][row];
        submission.desc.Transform[row][2] = worldMatrix.m[2][row];
        submission.desc.Transform[row][3] = worldMatrix.m[3][row];
    }

    submission.desc.InstanceID = static_cast<UINT>(submissions_.size());
    submission.desc.InstanceMask = 0xFF;
    submission.desc.InstanceContributionToHitGroupIndex = 0;
    submission.desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    submission.desc.AccelerationStructure = blasAddress;

    submissions_.push_back(submission);
}

void RaytracingScene::EnsureTlas()
{
    if (!IsActive() || tlasAttemptedThisFrame_)
    {
        return;
    }
    tlasAttemptedThisFrame_ = true;
    BuildTlas();
}

void RaytracingScene::BuildTlas()
{
    // 形が出来ている物だけを入れる。
    // スキニングのBLASは「積まれたが、まだ一度も作られていない」ことがあり、
    // 中身が未初期化のBLASを指したTLASを組むとGPUが壊れる
    instances_.clear();
    instances_.reserve(submissions_.size());
    for (const Submission &submission : submissions_)
    {
        if (submission.skinned)
        {
            auto it = skinnedBlasCache_.find(submission.pModel);
            if (it == skinnedBlasCache_.end() || !it->second.builtOnce)
            {
                continue;
            }
        }
        instances_.push_back(submission.desc);
    }

    if (instances_.empty())
    {
        return;
    }

    ID3D12GraphicsCommandList4 *pCommandList = pDxCommon_->GetCommandList4();

    // ── インスタンス記述をGPUへ送る ──
    const UINT64 required = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instances_.size();
    if (!instanceBuffer_ || instanceBufferCapacity_ < required)
    {
        // 作り直しを減らすため、必要量の倍を確保しておく
        instanceBufferCapacity_ = (std::max)(required * 2, static_cast<UINT64>(1024));
        instanceBuffer_ = pDxCommon_->CreateBufferResource(static_cast<size_t>(instanceBufferCapacity_));
    }
    {
        void *mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        if (SUCCEEDED(instanceBuffer_->Map(0, &readRange, &mapped)) && mapped)
        {
            std::memcpy(mapped, instances_.data(), static_cast<size_t>(required));
            instanceBuffer_->Unmap(0, nullptr);
        }
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    // 毎フレーム作り直すので、構築の速さを優先する
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.NumDescs = static_cast<UINT>(instances_.size());
    inputs.InstanceDescs = instanceBuffer_->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
    pDxCommon_->GetDevice5()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    if (prebuild.ResultDataMaxSizeInBytes == 0)
    {
        return;
    }

    // 置ける数が増えたときだけ作り直す
    const UINT64 scratchSize = AlignTo(prebuild.ScratchDataSizeInBytes,
                                       D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    const UINT64 resultSize = AlignTo(prebuild.ResultDataMaxSizeInBytes,
                                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    if (!tlasScratch_ || tlasScratch_->GetDesc().Width < scratchSize)
    {
        tlasScratch_ = CreateBuffer(scratchSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (!tlasResult_ || tlasResult_->GetDesc().Width < resultSize)
    {
        tlasResult_ = CreateBuffer(resultSize, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = tlasScratch_->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = tlasResult_->GetGPUVirtualAddress();

    pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    // 構築が終わるまでレイを飛ばさせない
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = tlasResult_.Get();
    pCommandList->ResourceBarrier(1, &barrier);

    tlasBuilt_ = true;

    // 加速構造の規模は起動直後に一度だけ出す。毎フレーム出すとログが溢れる
    static bool loggedOnce = false;
    if (!loggedOnce)
    {
        loggedOnce = true;
        Logger::Log(std::format(
            "RaytracingScene: TLAS を構築しました（形の種類={} / 配置数={} / スキン={}）\n",
            blasCache_.size(), instances_.size(), skinnedBlasCache_.size()));
    }
}
} // namespace Hagine
