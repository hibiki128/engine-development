#include "Object3dInstancing.h"
#include "DirectXCommon.h"
#include "transform/WorldTransform.h"
#include <cstring>
#include <render/deferred/DeferredRenderer.h>
#include <shadow/ShadowMap.h>

namespace Hagine {

void Object3dInstancing::Finalize()
{
    if (instanceResource_ && pInstanceData_)
    {
        instanceResource_->Unmap(0, nullptr);
    }
    pInstanceData_ = nullptr;
    instanceResource_.Reset();
    retiredInstanceResources_.clear();
    batches_.clear();
    instanceCapacity_ = 0;
    collecting_ = false;
}

void Object3dInstancing::BeginFrame()
{
    writeCursor_ = 0;
    // 統計は1フレーム（全パスぶん）で集計する
    lastBatchCount_ = 0;
    lastInstanceCount_ = 0;
    lastMergedDrawCount_ = 0;
}

void Object3dInstancing::Begin()
{
    // バッチはパスごとに作り直す。要素自体は残して確保済みメモリを使い回すが、
    // 前回まったく使われなかったキーは捨てる（マテリアルを毎フレーム変えるオブジェクトが
    // あるとシグネチャが毎回変わり、放置するとマップが際限なく増えるため）。
    for (auto it = batches_.begin(); it != batches_.end();)
    {
        if (!it->second.pRepresentative)
        {
            it = batches_.erase(it);
            continue;
        }
        it->second.instances.clear();
        it->second.pRepresentative = nullptr;
        ++it;
    }
    collecting_ = true;
}

bool Object3dInstancing::TrySubmit(Object3d *pObject3d, const WorldTransform &worldTransform,
                                   const ViewProjection &viewProjection, bool reflect, bool lighting)
{
    // Begin()〜Flush() の外から呼ばれた場合は積まない（積みっぱなしで描画されない事故を防ぐ）
    if (!enabled_ || !collecting_ || !pObject3d)
    {
        return false;
    }
    if (!pObject3d->CanBatchInstanced())
    {
        return false;
    }
    // このパスで描かないもの（G-Buffer で描き済み等）は積まない。
    // false を返しても Object3d::Draw が同じ判定で早期 return するので二重描画にはならない。
    if (!pObject3d->ShouldDrawInCurrentPass(lighting))
    {
        return false;
    }

    // 半透明は描画順で結果が変わるため、順序を入れ替えるインスタンシングは不透明だけに限る。
    // ただし影パス（深度のみ）と G-Buffer パス（不透明専用）は順序に依存しないので許可する。
    const bool shadowPass = ShadowMap::GetInstance()->IsShadowPassActive();
    const bool gBufferPass = DeferredRenderer::GetInstance()->IsGBufferPassActive();
    if (!shadowPass && !gBufferPass && pObject3d->GetBlendMode() != BlendMode::None)
    {
        return false;
    }

    const size_t key = pObject3d->ComputeBatchSignature(reflect, lighting);
    Batch &batch = batches_[key];
    if (!batch.pRepresentative)
    {
        batch.pRepresentative = pObject3d;
        batch.reflect = reflect;
        batch.lighting = lighting;
    }
    else if (batch.pRepresentative->GetModel() != pObject3d->GetModel())
    {
        // ハッシュ衝突（別モデルが同じキーになった）。まとめると別の形で描いてしまうので従来描画へ。
        return false;
    }

    ObjectInstanceData instance{};
    pObject3d->BuildInstanceMatrices(worldTransform, viewProjection,
                                     instance.wvp, instance.world,
                                     instance.worldInverseTranspose, instance.lightWVP);
    // 個体ごとの色。マテリアルが1つのときだけ有効で、そのときマテリアル定数バッファ側は白で送る。
    instance.color = pObject3d->UsesInstanceColor() ? pObject3d->GetColor(0)
                                                    : Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    batch.instances.push_back(instance);
    return true;
}

void Object3dInstancing::EnsureInstanceBuffer(size_t requiredCount)
{
    if (requiredCount <= instanceCapacity_ && instanceResource_)
    {
        return;
    }
    // 作り直しの頻度を抑えるため多めに確保する
    size_t newCapacity = (instanceCapacity_ > 0) ? instanceCapacity_ : 64;
    while (newCapacity < requiredCount)
    {
        newCapacity *= 2;
    }

    if (!pDxCommon_)
    {
        pDxCommon_ = DirectXCommon::GetInstance();
    }
    // 旧バッファは実行中のコマンドリストが参照している可能性があるため即解放しない
    if (instanceResource_)
    {
        instanceResource_->Unmap(0, nullptr);
        retiredInstanceResources_.push_back(instanceResource_);
    }
    instanceResource_ = pDxCommon_->CreateBufferResource(sizeof(ObjectInstanceData) * newCapacity);
    instanceResource_->Map(0, nullptr, reinterpret_cast<void **>(&pInstanceData_));
    instanceCapacity_ = newCapacity;
}

void Object3dInstancing::Flush(const ViewProjection &viewProjection)
{
    collecting_ = false;

    size_t totalInstances = 0;
    for (const auto &[key, batch] : batches_)
    {
        totalInstances += batch.instances.size();
    }
    if (totalInstances == 0)
    {
        return;
    }
    // 同じフレーム内の他パスが書いた領域は上書きしない（記録済みの描画がそこを参照している）
    EnsureInstanceBuffer(writeCursor_ + totalInstances);
    if (!pInstanceData_)
    {
        return;
    }

    const bool shadowPass = ShadowMap::GetInstance()->IsShadowPassActive();

    // 全バッチのインスタンスを1本のバッファへ連結し、バッチごとに先頭アドレスを渡して描く
    size_t writeIndex = writeCursor_;
    for (auto &[key, batch] : batches_)
    {
        if (!batch.pRepresentative || batch.instances.empty())
        {
            continue;
        }
        std::memcpy(pInstanceData_ + writeIndex, batch.instances.data(),
                    sizeof(ObjectInstanceData) * batch.instances.size());

        const D3D12_GPU_VIRTUAL_ADDRESS address =
            instanceResource_->GetGPUVirtualAddress() + sizeof(ObjectInstanceData) * writeIndex;
        const uint32_t instanceCount = static_cast<uint32_t>(batch.instances.size());

        if (shadowPass)
        {
            batch.pRepresentative->DrawShadowInstancedBatch(address, instanceCount);
        }
        else
        {
            batch.pRepresentative->DrawInstancedBatch(address, instanceCount, viewProjection,
                                                      batch.reflect, batch.lighting);
        }

        writeIndex += batch.instances.size();
        ++lastBatchCount_;
        lastInstanceCount_ += instanceCount;
    }
    writeCursor_ = writeIndex; // 次のパスはこの続きへ書く
    // まとめたことで減った描画コール数（1体ずつ描いていたときとの差）
    lastMergedDrawCount_ = (lastInstanceCount_ > lastBatchCount_) ? (lastInstanceCount_ - lastBatchCount_) : 0;
}
} // namespace Hagine
