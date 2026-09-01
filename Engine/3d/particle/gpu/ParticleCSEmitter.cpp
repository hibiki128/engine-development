#define NOMINMAX
#include "ParticleCSEmitter.h"
#include "ParticleCSFieldManager.h"
#include "ParticleCSGroupManager.h"
#include <graphics/pipeline/ComputePipelineManager.h>
#include <graphics/pipeline/PipelineManager.h>
#include <Frame.h>
#include <light/LightGroup.h>
#include <line/LineRenderer.h>
#include <render/deferred/DeferredRenderer.h>
#include <shadow/ShadowMap.h>
#include <particle/ParticleCommon.h>
#include <debug/profiler/GpuProfiler.h>
#include <algorithm>
#include <random>
#include "../utility/debug/imgui/ImGuizmoManager.h"
#include "../utility/debug/imgui/ImGuiNotification.h"
#include <object/base/BaseObject.h>
#include <transform/WorldTransform.h>

namespace Hagine {
namespace {
/// ワールド行列から回転成分だけを取り出す（各行を正規化してスケールを落とす。行ベクトル規約）。
/// 行0/1/2 がそれぞれ右/上/前のワールド方向になる。
Matrix4x4 ExtractRotation(const Matrix4x4 &world)
{
    Matrix4x4 rot = MakeIdentity4x4();
    for (int row = 0; row < 3; ++row)
    {
        Vector3 axis = {world.m[row][0], world.m[row][1], world.m[row][2]};
        const float len = axis.Length();
        if (len > 1e-6f)
        {
            axis = axis / len;
        }
        else
        {
            // 潰れた行はその軸の単位ベクトルで代用（0除算と行列の破綻を避ける）
            axis = {row == 0 ? 1.0f : 0.0f, row == 1 ? 1.0f : 0.0f, row == 2 ? 1.0f : 0.0f};
        }
        rot.m[row][0] = axis.x;
        rot.m[row][1] = axis.y;
        rot.m[row][2] = axis.z;
    }
    return rot;
}
} // namespace

std::vector<ParticleCSEmitter *> ParticleCSEmitter::liveEmitters_;

ParticleCSEmitter::ParticleCSEmitter()
{
    liveEmitters_.push_back(this);
}

ParticleCSEmitter::~ParticleCSEmitter()
{
    liveEmitters_.erase(std::remove(liveEmitters_.begin(), liveEmitters_.end(), this), liveEmitters_.end());

#ifdef USE_IMGUI
    // ギズモには pEmitterMeshData_ の中身への生ポインタを渡しているので、
    // 解除し忘れると破棄後のメモリを掴んだままになる。
    // 同名で登録し直されている場合（テンプレートを開き直した等）は相手の登録を消さない。
    if (gizmoRegistered_ && pEmitterMeshData_)
    {
        ImGuizmoManager::GetInstance()->RemoveTargetIfOwnedBy(name_, &pEmitterMeshData_->translate);
    }
    gizmoRegistered_ = false;
#endif // USE_IMGUI

    // 保有していた独立グループを破棄せず再利用プールへ返却する。
    // これにより弾・ヒット等の高頻度スポーンでもバッファが累積しない。
    // 注意: group は Finalize 順序によっては既に破棄済みの場合がある。
    // ReleaseIndependentGroup はポインタ比較のみで deref しないため安全。
    for (ParticleCSGroup *group : particleGroups_)
    {
        if (group)
        {
            ParticleCSGroupManager::GetInstance()->ReleaseIndependentGroup(group);
        }
    }
    particleGroups_.clear();
    particleGroupNames_.clear();
}

void ParticleCSEmitter::Initialize(const std::string &name)
{
    pParticleCommon_ = ParticleCommon::GetInstance();
    pDxCommon_ = ParticleCommon::GetInstance()->GetDxCommon();
    pCommandList_ = pDxCommon_->GetCommandList().Get();
    pSrvManager_ = SrvManager::GetInstance();
    name_ = name;
    CreateEmitterMeshResource();
    LoadSetting();
#ifdef USE_IMGUI
    // 実行時に量産されるインスタンス(registerGizmo_=false)は登録しない。
    // 登録するとエディタ上の同名テンプレートのギズモ対象を奪ってしまう。
    if (registerGizmo_ && pEmitterMeshData_)
    {
        ImGuizmoManager::GetInstance()->AddTarget(
            name_,
            &pEmitterMeshData_->translate,
            nullptr, // 必要に応じて回転のVector3を追加
            &pEmitterMeshData_->scale,
            isGizmoSelectable_);
        // 実際に登録したかを覚えておく（registerGizmo_ は後から変更されうるため、
        // デストラクタでの解除判定にはこちらを使う）
        gizmoRegistered_ = true;
    }
#endif
}

void ParticleCSEmitter::Initialize(const std::string &name, const std::string &modelPath)
{
    Initialize(name);
    modelPath_ = modelPath;
    LoadModel(modelPath);
    CreateModelTriangles();
    CreateModelEdges();
}

void ParticleCSEmitter::Initialize(const std::string &name, PrimitiveType primitiveType)
{
    Initialize(name);
    primitiveType_ = primitiveType;
    LoadPrimitiveModel(primitiveType);
    CreateModelTriangles();
    CreateModelEdges();
}

void ParticleCSEmitter::SetParent(BaseObject *parent)
{
    pParentTransform_ = parent ? parent->GetWorldTransform() : nullptr;
}

void ParticleCSEmitter::ResolveEmitterTransform(const ViewProjection &vp)
{
    // カメラのワールド回転。matWorld_ の行0/1/2 が右/上/前のワールド方向。
    cameraRotation_ = ExtractRotation(vp.matWorld_);

    if (!pEmitterMeshData_)
        return;

    // ---- 位置 ----
    Matrix4x4 parentRot = MakeIdentity4x4();
    if (pParentTransform_)
    {
        const Matrix4x4 &pw = pParentTransform_->matWorld_;
        parentRot = ExtractRotation(pw);
        const Vector3 parentPos = {pw.m[3][0], pw.m[3][1], pw.m[3][2]};
        // 親のスケールは意図しない拡大を避けるため乗せない（回転済みオフセットのみ足す）
        pEmitterMeshData_->translate = parentPos + TransformNormal(localTranslate_, parentRot);
    }

    // ---- 向き ----
    if (!billboardEmitter_ && !pParentTransform_)
    {
        pEmitterMeshData_->rotation = baseRotation_; // 合成不要（毎フレームの再変換による誤差も避ける）
        return;
    }

    // ローカル → 親（またはカメラ）の順に適用する（行ベクトル規約なので行列積もこの順）。
    // QuaternionToMatrix4x4 と Quaternion::FromMatrix は互いに逆変換なので、
    // 合成結果をそのままエミッターの回転クォータニオンへ戻せる。
    // ビルボードONのときは「常にカメラへ正対」が目的なので親の回転は使わない
    // （位置だけ親に追従し、向きはカメラが決める）。
    Matrix4x4 resolved = QuaternionToMatrix4x4(baseRotation_);
    resolved = resolved * (billboardEmitter_ ? cameraRotation_ : parentRot);
    pEmitterMeshData_->rotation = Quaternion::FromMatrix(resolved).Normalize();
}

void ParticleCSEmitter::SubmitEmissiveLight()
{
    // エディタの編集用エミッターはプレビュー窓にしか描かれないので、ゲームシーンは照らさない
    if (previewOnly_)
        return;
    if (!lightEnabled_ || !isActive_ || !pEmitterMeshData_)
        return;
    if (lightIntensity_ <= 0.0f || lightRadius_ <= 0.0f)
        return;

    if (lightFollowParticles_)
    {
        // 撃ち終わった弾がその場で光り続けないよう、粒子が無くなったら消灯する。
        // 生存数は readback ベースで1〜2フレーム遅延するが、消灯が数フレーム遅れる程度で害はない。
        const bool emitting = (pEmitterMeshData_->emit != 0);
        bool anyAlive = false;
        for (const ParticleCSGroup *group : particleGroups_)
        {
            if (group->GetAliveDrawCount() > 0)
            {
                anyAlive = true;
                break;
            }
        }
        if (!emitting && !anyAlive)
            return;
    }

    // オフセットはエミッターの向きに追従させる（親に付けた発光位置がずれないように）
    LightGroup::DynamicPointLightDesc desc;
    desc.position = pEmitterMeshData_->translate + RotateVector(lightOffset_, pEmitterMeshData_->rotation);
    desc.color = lightColor_;
    desc.intensity = lightIntensity_;
    desc.radius = lightRadius_;
    desc.decay = lightDecay_;
    LightGroup::GetInstance()->AddDynamicPointLight(desc);
}

void ParticleCSEmitter::EnsureParticleLightResource()
{
    if (particleLightCBResource_ || !pDxCommon_)
    {
        return;
    }
    particleLightCBResource_ = pDxCommon_->CreateBufferResource(sizeof(ParticleLightGenConstants));
    particleLightCBResource_->Map(0, nullptr, reinterpret_cast<void **>(&pParticleLightCBData_));
    *pParticleLightCBData_ = {};
}

bool ParticleCSEmitter::SubmitParticleLights(const ViewProjection &vp, ID3D12GraphicsCommandList *pCommandList)
{
    // エディタの編集用エミッターはプレビュー窓にしか描かれないので、ゲームシーンは照らさない。
    // 発生が止まっていても残っている粒子は光らせる（生存0なら下のループが自然に空振りする）。
    if (previewOnly_ || !particleLightEnabled_ || !pCommandList)
        return false;
    if (particleGroups_.empty() || particleLightMaxCount_ == 0)
        return false;
    if (particleLightIntensity_ <= 0.0f || particleLightRadius_ <= 0.0f)
        return false;

    EnsureParticleLightResource();
    if (!pParticleLightCBData_)
        return false;

    const uint32_t stride = (std::max)(1u, particleLightStride_);

    // 生成パラメータは全グループで共通なので、CBは1本を使い回す
    ParticleLightGenConstants &cb = *pParticleLightCBData_;
    cb.particleStride = stride;
    cb.maxLights = particleLightMaxCount_;
    cb.bufferCapacity = LightGroup::kMaxBufferedPointLights;
    cb.useParticleColor = particleLightUseParticleColor_ ? 1u : 0u;
    cb.lightColor = {particleLightColor_.x, particleLightColor_.y, particleLightColor_.z};
    cb.intensity = particleLightIntensity_;
    cb.radius = particleLightRadius_;
    cb.decay = particleLightDecay_;
    cb.cullDistance = particleLightCullDistance_;
    cb.alphaCutoff = 0.02f; // ほぼ消えている粒子は光源にしない
    cb.cameraPosition = vp.translation_;

    bool dispatched = false;
    for (ParticleCSGroup *group : particleGroups_)
    {
        if (!group)
            continue;
        // 光源化は描画リスト(gRenderCompact)を読むので、長さも描画リスト長(可視カウンタ)を使う。
        // ＝視錐台カリングが有効なら画面外の粒子は光源にならない（画面に効かない光源を省く）。
        const D3D12_GPU_VIRTUAL_ADDRESS compactAddress = group->GetRenderCompactGpuAddress();
        const D3D12_GPU_VIRTUAL_ADDRESS visibleCountAddress = group->GetVisibleCounterGpuAddress();
        if (compactAddress == 0 || visibleCountAddress == 0)
            continue;

        // ディスパッチ数は前フレームに読み戻した生存数から決める（1〜2フレーム遅延）。
        // 少なく見積もると光源が減るだけ、多く見積もってもシェーダ側が
        // GPU上の生存数で弾くので、描画と同じくマージンを乗せておく。
        uint32_t alive = group->GetAliveDrawCount();
        alive = alive + (alive / 4) + 64;
        const uint32_t maxCount = group->GetSettingsData()->maxParticleCount;
        if (alive > maxCount)
            alive = maxCount;
        if (alive == 0)
            continue;

        uint32_t lightCount = (alive + stride - 1) / stride;
        lightCount = (std::min)(lightCount, particleLightMaxCount_);
        if (lightCount == 0)
            continue;

        ComputePipelineManager::GetInstance()->DrawCommonSetting(
            ComputePipelineType::ParticleLightGen, BlendMode::Normal, ShaderMode::None, pCommandList);

        LightGroup *lightGroup = LightGroup::GetInstance();
        pCommandList->SetComputeRootConstantBufferView(0, particleLightCBResource_->GetGPUVirtualAddress());
        pCommandList->SetComputeRootShaderResourceView(1, compactAddress);
        pCommandList->SetComputeRootShaderResourceView(2, visibleCountAddress);
        pCommandList->SetComputeRootUnorderedAccessView(3, lightGroup->GetPointLightUavAddress());
        pCommandList->SetComputeRootUnorderedAccessView(4, lightGroup->GetLightCounterAddress());

        constexpr uint32_t kThreadsPerGroup = 64; // ParticleLightGen.CS.hlsl の [numthreads] と一致必須
        pCommandList->Dispatch((lightCount + kThreadsPerGroup - 1) / kThreadsPerGroup, 1, 1);
        dispatched = true;
    }
    return dispatched;
}

bool ParticleCSEmitter::SubmitAllParticleLights(const ViewProjection &vp, ID3D12GraphicsCommandList *pCommandList)
{
    bool dispatched = false;
    for (ParticleCSEmitter *pEmitter : liveEmitters_)
    {
        if (pEmitter && pEmitter->SubmitParticleLights(vp, pCommandList))
        {
            dispatched = true;
        }
    }
    return dispatched;
}

Matrix4x4 ParticleCSEmitter::GetEffectSpaceMatrix(uint32_t effectSpace) const
{
    switch (effectSpace)
    {
    case 1: // エミッター基準（ビルボードON時はカメラ回転も既に含まれている）
        return pEmitterMeshData_ ? QuaternionToMatrix4x4(pEmitterMeshData_->rotation) : MakeIdentity4x4();
    case 2: // ビルボード（カメラの向きに追従）
        return cameraRotation_;
    default: // 0 = ワールド固定（従来動作）
        return MakeIdentity4x4();
    }
}

void ParticleCSEmitter::DrawCompute(const ViewProjection &vp)
{
    if (ShadowMap::GetInstance()->IsShadowPassActive())
        return;

    // 発生源メッシュの位置・向き（親追従＝ビルボード）を Emit/Update のディスパッチより前に確定させる。
    // グループが空でもワイヤーフレーム表示のために解決しておく。
    ResolveEmitterTransform(vp);

    // 位置が確定した直後に発光を登録する。DrawSystem がこの後
    // CommitPointLights() を呼ぶので、同じフレームの描画へ反映される。
    SubmitEmissiveLight();

    if (particleGroups_.empty())
        return;

    // --- アイドルグループのスキップ判定 ---
    // 「前フレームの生存数(readback)が0」かつ「今フレーム発生しない」グループは、
    // Reset/Emit/Update/Readback のGPUディスパッチ・バリア・readbackコピーをまるごと省く。
    // これでグループごとの固定オーバーヘッド（最低4096スレッドのUpdate + バリア数本 + CopyResource）が消える。
    //
    // 安全性: 判定に使う aliveDrawCount_ は readback で1〜2フレーム遅延する。
    //   ・発生を止めた直後は遅延中 生存数>0 のまま → スキップせず drain し切るので取りこぼしなし。
    //   ・生存0が観測できた時点で両ping-pongカウンタは既に0 → 凍結しても位相/カウンタは不整合にならない。
    //   ・フィールド接触発生(emitOnlyOnFieldContact_)はGPU判定でCPUから確定できないため常時アクティブ扱い(安全側)。
    groupActive_.assign(particleGroups_.size(), 1);
    const bool fieldSpawnPossible = emitOnlyOnFieldContact_ && receiveFields_;
    const bool emitterEmitting = (pEmitterMeshData_->emit != 0);
    bool anyActive = false;
    for (size_t i = 0; i < particleGroups_.size(); ++i)
    {
        ParticleCSGroup *group = particleGroups_[i];
        // aliveDrawCount_ は前フレームの DrawGraphics/プレビューが全グループで更新済み(1〜2F遅延)。
        // ここで再度 FetchAliveDrawCount(Map/Unmap) すると全グループ分の余計なCPUコストになるので既存値を使う。
        const bool willEmit = fieldSpawnPossible ||
                              (emitterEmitting && group->GetSettingsData()->emitCount > 0);
        const bool active = willEmit || (group->GetAliveDrawCount() > 0);
        groupActive_[i] = active ? 1u : 0u;
        anyActive = anyActive || active;
    }
    // 全グループがアイドルなら、このエミッターのコンピュートは丸ごと不要（グローバルバリアも省ける）。
    if (!anyActive)
        return;

    auto *computeCmdList = pDxCommon_->GetComputeCommandList().Get();
    pDxCommon_->BeginComputeFrame();

    auto *fieldMgr = ParticleCSFieldManager::GetInstance();
    auto fieldCountRes = receiveFields_ ? fieldMgr->GetFieldCountResource()
                                        : fieldMgr->GetZeroFieldCountResource();
    // このグループ群がフィールドの影響を受けるか（軽量 Update 適格判定に使う）。
    // receiveFields_=false なら shader へ渡る fieldCount は 0 なので影響なし扱い。
    const bool fieldsActive = receiveFields_ && (fieldMgr->GetActiveFieldCount() > 0);

    // 生存リスト間接ディスパッチ:
    //   Emit と Update がどちらも out リストへ append するため、フレーム順序は
    //   「reset(out) → Emit(append) → barrier → Update(read in, append out) → readback」。
    //   Emit は全グループを一括ディスパッチするので、reset/フェーズ反転は全グループ分先に行う。

    // 1) 各グループ: CPU更新 + フェーズ反転 + out カウンタを 0 リセット（Emit の append より前）
    //    アイドルグループはここも省く（位相を進めず凍結する）。
    for (size_t i = 0; i < particleGroups_.size(); ++i)
    {
        if (!groupActive_[i])
            continue;
        auto &group = particleGroups_[i];
        // Trail/Rotation/Override バッファは Emit CS も書き込むため、Emit のバインドより前に本確保する。
        // Update 内の確保だけだと、初回 Emit がダミーバッファへ書いて OOB で破棄され、
        // その粒子のトレイル状態（lastTrailPosition 等）が未初期化になる
        // （花火が偶にトレイル無しで打ち上がる不具合の原因）
        group->EnsureUpdateOptionalBuffers(fieldsActive);
        // エディタの編集用エミッターはプレビュー窓（＝別カメラ）にしか描かないので、
        // シーンカメラの視錐台でカリングすると編集中の粒子が消える。ここで抑止する。
        group->SetFrustumCullSuppressed(previewOnly_);
        group->Update(vp);
        group->AdvanceAliveFrame();
        group->ResetAliveCounterDispatch(computeCmdList);
    }

    // 2) Emit（全グループ一括）: 新規粒子を out リスト/renderCompact へ append
    int emitSpan = GpuProfiler::GetInstance()->OpenCompute(computeCmdList, "Emit");
    EmitterDisPatch(computeCmdList);
    GpuProfiler::GetInstance()->Close(computeCmdList, emitSpan);

    // 3) Emit が書いた SoA6本 + 生存リストを Update の前に可視化（グローバル UAV バリア）
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = nullptr;
    computeCmdList->ResourceBarrier(1, &uavBarrier);

    // 4) 各グループ: Update（in リストを sim し survivor を out へ append）
    int updateSpan = GpuProfiler::GetInstance()->OpenCompute(computeCmdList, "Update");
    for (size_t i = 0; i < particleGroups_.size(); ++i)
    {
        if (!groupActive_[i])
            continue;
        particleGroups_[i]->UpdateParticleCSDisPatch(
            fieldMgr->GetFieldsSrvHandle(),
            fieldCountRes,
            fieldMgr->GetOverrideSrvHandle(),
            fieldsActive,
            computeCmdList);
    }
    GpuProfiler::GetInstance()->Close(computeCmdList, updateSpan);

    // 5) 各グループ: 生存数(out カウンタ)を readback バッファへコピー（compute キュー上で記録）
    for (size_t i = 0; i < particleGroups_.size(); ++i)
    {
        if (!groupActive_[i])
            continue;
        particleGroups_[i]->RecordAliveCountReadback(computeCmdList);
    }
    // Execute は DrawSystem（または呼び出し元）が一括で行う
}

void ParticleCSEmitter::DrawGraphics(const ViewProjection &vp)
{
    // パーティクルは半透明なのでG-Bufferには載せず、前方描画フェーズで描く
    if (ShadowMap::GetInstance()->IsShadowPassActive() ||
        DeferredRenderer::GetInstance()->IsGBufferPassActive())
        return;
    if (particleGroups_.empty())
        return;

    DrawEmitter();

    int drawSpan = GpuProfiler::GetInstance()->OpenGraphics(pCommandList_, "Draw");
    for (auto &group : particleGroups_)
    {
        // 旧 CountParticle 全Nディスパッチは廃止（生存数は aliveCounter に統合）

        // 生存数の読み戻しは統計・Update のディスパッチ本数見積り用（描画数の決定には使わない）。
        // 描画は GPU 駆動（ExecuteIndirect）で、instanceCount は VRAM 上のカウンタから直接読む。
        group->FetchAliveDrawCount();
        // 引数バッファへ一度も書き込みが記録されていないなら描画しない（未初期化引数の実行防止）。
        // 生成直後の1フレームだけで、そのとき粒子は0個なので見た目に影響はない。
        if (!group->IsDrawArgsReady())
        {
            continue;
        }

        pParticleCommon_->GPUDrawCommonSetting(group->GetParticleGroupData().blendMode);
        // t0 は頂点シェーダー(描画リスト)とピクセルシェーダー(テクスチャ)で同番号の別リソース
        const ShaderRootSignature *gpuParticleRS =
            PipelineManager::GetInstance()->GetReflectedRootSignature(PipelineType::GPUParticle);
        assert(gpuParticleRS && "GPUパーティクルのルートシグネチャが未生成です");
        const auto &meshes = group->GetModelData().meshes;
        for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
        {
            D3D12_INDEX_BUFFER_VIEW indexBufferView = group->GetIndexBufferView();
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = group->GetVertexBufferView();
            pCommandList_->IASetIndexBuffer(&indexBufferView);
            pCommandList_->IASetVertexBuffers(0, 1, &vertexBufferView);
            pCommandList_->SetGraphicsRootConstantBufferView(gpuParticleRS->GetCbvIndex(0, D3D12_SHADER_VISIBILITY_VERTEX), group->GetPerViewResource()->GetGPUVirtualAddress());
            // 描画コンパクション: t0=詰めた描画バッファ(順次読み), t4=Rotation(回転グループのみscatter)
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(0, D3D12_SHADER_VISIBILITY_VERTEX), group->GetRenderCompactSrvForVSIndex());
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(0, D3D12_SHADER_VISIBILITY_PIXEL), TextureManager::GetInstance()->GetTextureIndexByFilePath(group->GetParticleGroupData().materials[meshIndex].textureFilePath));
            pCommandList_->SetGraphicsRootConstantBufferView(gpuParticleRS->GetCbvIndex(1, D3D12_SHADER_VISIBILITY_PIXEL), group->GetMaterialResource()->GetGPUVirtualAddress());
            // 描画リスト SRV (t2: renderSlot=描画順->slot, t3: visibleCount=描画リスト長)
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(2, D3D12_SHADER_VISIBILITY_VERTEX), group->GetRenderSlotSrvForVSIndex());
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(3, D3D12_SHADER_VISIBILITY_VERTEX), group->GetVisibleCounterSrvForVSIndex());
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(4, D3D12_SHADER_VISIBILITY_VERTEX), group->GetRotationSrvForVSIndex());
            ExecuteIndirectDraw(group, meshIndex);
        }
    }
    GpuProfiler::GetInstance()->Close(pCommandList_, drawSpan);
}

void ParticleCSEmitter::ExecuteIndirectDraw(ParticleCSGroup *group, size_t meshIndex)
{
    // GPU版 DrawInstance: 「今何個描くか」は引数バッファ内の InstanceCount（Compute が
    // GPU 上のカウンタからコピーした値）が決める。CPU は固定の描画命令を投げるだけ。
    ID3D12CommandSignature *signature = pParticleCommon_->GetDrawIndexedCommandSignature();
    if (!signature)
    {
        return;
    }
    const UINT64 argsOffset = static_cast<UINT64>(ParticleCSGroup::kDrawArgsStride) * meshIndex;
    pCommandList_->ExecuteIndirect(signature, 1, group->GetDrawArgsResource(), argsOffset, nullptr, 0);
}

void ParticleCSEmitter::Draw(const ViewProjection &vp)
{
    // 後方互換: 単体で呼ばれる場合は Compute→Execute→Wait→Graphics を自前で完結させる
    DrawCompute(vp);
    pDxCommon_->ExecuteComputeCommands();
    pDxCommon_->WaitForComputeOnDirectQueue();
    DrawGraphics(vp);
}

void ParticleCSEmitter::DrawGraphicsForPreview(D3D12_GPU_VIRTUAL_ADDRESS perViewGpuAddress,
                                               PerView *previewPerView,
                                               const Vector3 &cameraPos,
                                               float projScaleY)
{
    if (ShadowMap::GetInstance()->IsShadowPassActive())
        return;
    if (particleGroups_.empty())
        return;

    // ワイヤーフレーム(DrawEmitter)はプレビューでは描かない。
    int drawSpan = GpuProfiler::GetInstance()->OpenGraphics(pCommandList_, "Draw(プレビュー)");
    for (auto &group : particleGroups_)
    {
        // 描画カリング(距離/サイズ)をプレビューでも効かせる。プレビューは独立 per-view CB を
        // 使うため、グループの設定とプレビューカメラ位置/射影をここで per-view へ反映する。
        // （単一バッファなので複数グループ時は最後のグループ設定が全体に効く＝プレビューの簡略許容）
        if (previewPerView)
        {
            const PerView *gpv = group->GetPerView();
            previewPerView->cameraPosition = cameraPos;
            previewPerView->projScaleY = projScaleY;
            previewPerView->enableDistanceCull = gpv->enableDistanceCull;
            previewPerView->distanceCullStart = gpv->distanceCullStart;
            previewPerView->distanceCullEnd = gpv->distanceCullEnd;
            previewPerView->enableSizeClamp = gpv->enableSizeClamp;
            previewPerView->maxScreenHeight = gpv->maxScreenHeight;
            previewPerView->minScreenHeight = gpv->minScreenHeight;
            // 速度ストレッチもプレビューへ反映（これが無いとプレビューで引き伸ばしを確認できない）。
            previewPerView->enableVelocityStretch = gpv->enableVelocityStretch;
            previewPerView->velocityStretchFactor = gpv->velocityStretchFactor;
            // ビルボードのON/OFFもプレビューへ反映（OFFなら非ビルボード表示にする）。
            previewPerView->enableBillboard = gpv->enableBillboard;
        }
        group->FetchAliveDrawCount();
        if (!group->IsDrawArgsReady())
        {
            continue; // 引数バッファ未初期化（生成直後の1フレーム）
        }

        pParticleCommon_->GPUDrawCommonSetting(group->GetParticleGroupData().blendMode);
        // t0 は頂点シェーダー(描画リスト)とピクセルシェーダー(テクスチャ)で同番号の別リソース
        const ShaderRootSignature *gpuParticleRS =
            PipelineManager::GetInstance()->GetReflectedRootSignature(PipelineType::GPUParticle);
        assert(gpuParticleRS && "GPUパーティクルのルートシグネチャが未生成です");
        const auto &meshes = group->GetModelData().meshes;
        for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
        {
            D3D12_INDEX_BUFFER_VIEW indexBufferView = group->GetIndexBufferView();
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = group->GetVertexBufferView();
            pCommandList_->IASetIndexBuffer(&indexBufferView);
            pCommandList_->IASetVertexBuffers(0, 1, &vertexBufferView);
            // root param 0 のみプレビュー専用 per-view CB に差し替える（共有グループの VP は不変）
            pCommandList_->SetGraphicsRootConstantBufferView(gpuParticleRS->GetCbvIndex(0, D3D12_SHADER_VISIBILITY_VERTEX), perViewGpuAddress);
            // 描画コンパクション: t0=詰めた描画バッファ, t4=Rotation(回転グループのみ)
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(0, D3D12_SHADER_VISIBILITY_VERTEX), group->GetRenderCompactSrvForVSIndex());
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(0, D3D12_SHADER_VISIBILITY_PIXEL), TextureManager::GetInstance()->GetTextureIndexByFilePath(group->GetParticleGroupData().materials[meshIndex].textureFilePath));
            pCommandList_->SetGraphicsRootConstantBufferView(gpuParticleRS->GetCbvIndex(1, D3D12_SHADER_VISIBILITY_PIXEL), group->GetMaterialResource()->GetGPUVirtualAddress());
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(2, D3D12_SHADER_VISIBILITY_VERTEX), group->GetRenderSlotSrvForVSIndex());
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(3, D3D12_SHADER_VISIBILITY_VERTEX), group->GetVisibleCounterSrvForVSIndex());
            pSrvManager_->SetGraphicsRootDescriptorTable(gpuParticleRS->GetSrvIndex(4, D3D12_SHADER_VISIBILITY_VERTEX), group->GetRotationSrvForVSIndex());
            ExecuteIndirectDraw(group, meshIndex);
        }
    }
    GpuProfiler::GetInstance()->Close(pCommandList_, drawSpan);
}

void ParticleCSEmitter::LoadModel(const std::string &modelPath)
{
    ModelManager::GetInstance()->LoadModel(modelPath);
    pModel_ = ModelManager::GetInstance()->FindModel(modelPath);
    if (pModel_)
    {
        modelData_ = pModel_->GetModelData();
    }
}

void ParticleCSEmitter::LoadPrimitiveModel(PrimitiveType type)
{
    // 円形系(Ring/Sphere/Cylinder/Cone)は分割数/半径パラメータを反映して生成する。
    std::string modelKey = IsParametricPrimitive(type)
                               ? ModelManager::GetInstance()->CreatePrimitiveModel(type, "", primitiveParams_)
                               : ModelManager::GetInstance()->CreatePrimitiveModel(type, "");
    pModel_ = ModelManager::GetInstance()->FindModel(modelKey);
    if (pModel_)
    {
        modelData_ = pModel_->GetModelData();
    }
}

void ParticleCSEmitter::RebuildPrimitiveModel()
{
    if (primitiveType_ == PrimitiveType::None)
        return;
    // モデルを作り直し、発生面(三角形)・エッジも新しい形状で再構築する。
    LoadPrimitiveModel(primitiveType_);
    CreateModelTriangles();
    CreateModelEdges();
}

void ParticleCSEmitter::Update()
{
    if (isAuto_)
    {
        EmitterUpdate();
    }
    else if (emitOnce_)
    {
        pEmitterMeshData_->emit = 1;
        emitOnce_ = false;
    }
    else
    {
        pEmitterMeshData_->emit = 0;
    }
}

void ParticleCSEmitter::EmitOnce()
{
    emitOnce_ = true;
}

void ParticleCSEmitter::DrawEmitter()
{
    if (!isVisible_)
        return;
    Vector3 translate = pEmitterMeshData_->translate;
    Quaternion rotation = pEmitterMeshData_->rotation;
    Vector3 scale = pEmitterMeshData_->scale;

    Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
    Matrix4x4 rotateMatrix = MakeRotateXYZMatrix(rotation);
    Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);
    Matrix4x4 transformMatrix = MakeAffineMatrix(scale, rotation, translate);

    if (pEmitterMeshData_->emitFromSurface == 2 && !edgeInfoList_.empty())
    {
        Vector4 color = {1.0f, 0.5f, 0.0f, 1.0f};
        for (const auto &edge : edgeInfoList_)
        {
            Vector3 v0 = Transformation(edge.v0, transformMatrix);
            Vector3 v1 = Transformation(edge.v1, transformMatrix);
            LineRenderer::GetInstance()->AddLine(v0, v1, {1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    else if (!triangleInfoList_.empty())
    {
        Vector4 color = {0.0f, 1.0f, 0.0f, 1.0f};
        for (const auto &tri : triangleInfoList_)
        {
            Vector3 v0 = Transformation(tri.v0, transformMatrix);
            Vector3 v1 = Transformation(tri.v1, transformMatrix);
            Vector3 v2 = Transformation(tri.v2, transformMatrix);

            LineRenderer::GetInstance()->AddLine(v0, v1, {1.0f, 1.0f, 1.0f, 1.0f});
            LineRenderer::GetInstance()->AddLine(v1, v2, {1.0f, 1.0f, 1.0f, 1.0f});
            LineRenderer::GetInstance()->AddLine(v2, v0, {1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    else
    {
        Vector3 center = pEmitterMeshData_->translate;
        Vector4 color = {1.0f, 1.0f, 0.0f, 1.0f};
        float maxRadius = std::max(std::max(scale.x, scale.y), scale.z);
        LineRenderer::GetInstance()->AddSphere(center, maxRadius, color, 16);
    }
}

std::vector<ParticleCSEmitter::WireSegment> ParticleCSEmitter::GetWireframeSegments() const
{
    std::vector<WireSegment> segs;
    if (!pEmitterMeshData_)
    {
        return segs;
    }
    Vector3 translate = pEmitterMeshData_->translate;
    Quaternion rotation = pEmitterMeshData_->rotation;
    Vector3 scale = pEmitterMeshData_->scale;
    Matrix4x4 transformMatrix = MakeAffineMatrix(scale, rotation, translate);

    if (pEmitterMeshData_->emitFromSurface == 2 && !edgeInfoList_.empty())
    {
        const Vector4 color = {1.0f, 0.5f, 0.0f, 1.0f}; // 橙: エッジ
        for (const auto &edge : edgeInfoList_)
        {
            segs.push_back({Transformation(edge.v0, transformMatrix),
                            Transformation(edge.v1, transformMatrix), color});
        }
    }
    else if (!triangleInfoList_.empty())
    {
        const Vector4 color = {0.0f, 1.0f, 0.0f, 1.0f}; // 緑: 三角形
        for (const auto &tri : triangleInfoList_)
        {
            Vector3 v0 = Transformation(tri.v0, transformMatrix);
            Vector3 v1 = Transformation(tri.v1, transformMatrix);
            Vector3 v2 = Transformation(tri.v2, transformMatrix);
            segs.push_back({v0, v1, color});
            segs.push_back({v1, v2, color});
            segs.push_back({v2, v0, color});
        }
    }
    else
    {
        // メッシュ無し: 半径を示す球ワイヤー（XY/XZ/YZ の3円）
        const Vector3 center = pEmitterMeshData_->translate;
        const Vector4 color = {1.0f, 1.0f, 0.0f, 1.0f}; // 黄: 球
        const float r = std::max(std::max(scale.x, scale.y), scale.z);
        const int kCircleSegmentCount = 24;
        const float twoPi = 6.28318530718f;
        for (int i = 0; i < kCircleSegmentCount; ++i)
        {
            float a0 = twoPi * static_cast<float>(i) / kCircleSegmentCount;
            float a1 = twoPi * static_cast<float>(i + 1) / kCircleSegmentCount;
            float c0 = std::cos(a0), s0 = std::sin(a0);
            float c1 = std::cos(a1), s1 = std::sin(a1);
            segs.push_back({{center.x + c0 * r, center.y + s0 * r, center.z},
                            {center.x + c1 * r, center.y + s1 * r, center.z},
                            color}); // XY
            segs.push_back({{center.x + c0 * r, center.y, center.z + s0 * r},
                            {center.x + c1 * r, center.y, center.z + s1 * r},
                            color}); // XZ
            segs.push_back({{center.x, center.y + c0 * r, center.z + s0 * r},
                            {center.x, center.y + c1 * r, center.z + s1 * r},
                            color}); // YZ
        }
    }
    return segs;
}

void ParticleCSEmitter::AddParticleGroup(ParticleCSGroup *group)
{
    if (!group)
        return;
    const std::string &name = group->GetGroupName();
    ParticleCSGroup *independentGroup = ParticleCSGroupManager::GetInstance()->GetIndependentParticleGroup(name);
    if (!independentGroup)
    {
        return;
    }
    independentGroup->SetSettingData(*group->GetSettingsData());
    // カラーグラデーションのストップ列は settings とは別ストレージなので別途コピーし、
    // 独立グループ側で 256段 LUT を再ベイクさせる（コピーしないと既定ストップでベイクされ崩れる）。
    independentGroup->GetColorStops() = group->GetColorStops();
    independentGroup->MarkColorStopsDirty();
    // 寿命カーブの制御点も別ストレージなので伝播させ、独立グループ側で再ベイクさせる。
    independentGroup->GetSizeCurvePoints() = group->GetSizeCurvePoints();
    independentGroup->GetAlphaCurvePoints() = group->GetAlphaCurvePoints();
    independentGroup->MarkLifeCurvesDirty();
    independentGroup->SetBlendMode(group->GetParticleGroupData().blendMode);
    independentGroup->SetBillboard(group->GetPerView()->enableBillboard);
    // テクスチャ(materials)は settings とは別ストレージなので明示的に伝播させる。
    // これがないと差し替えたテクスチャが描画対象の独立グループに反映されない。
    // GetParticleGroupData() は値返しなのでローカルに受けてから参照する。
    ParticleCSGroupData srcData = group->GetParticleGroupData();
    if (!srcData.materials.empty())
    {
        independentGroup->SetTexture(srcData.materials[0].textureFilePath);
    }
    particleGroups_.push_back(independentGroup);
    particleGroupNames_.insert(name);
    ImGuiNotification::Post("パーティクルグループを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleCSEmitter::RemoveParticleGroup(const std::string &groupName)
{
    auto it = std::remove_if(particleGroups_.begin(), particleGroups_.end(),
                             [&](ParticleCSGroup *group) {
                                 return group->GetGroupName() == groupName;
                             });
    if (it != particleGroups_.end())
    {
        particleGroups_.erase(it, particleGroups_.end());
    }
    particleGroupNames_.erase(groupName);
    ImGuiNotification::Post("パーティクルグループを削除しました: " + groupName, {0.9f, 0.7f, 0.2f, 1.0f});
}

void ParticleCSEmitter::EmitterUpdate()
{
    // フィールド接触Emitモードでは発生タイミングをフィールド側の間隔タイマーが管理する。
    // エミッターの frequency ゲートを重ねると2重の間隔制御になって分かりづらいため、
    // ここでは常に許可してフィールド側だけをタイミングの決定者にする。
    if (emitOnlyOnFieldContact_ && receiveFields_)
    {
        pEmitterMeshData_->emit = 1;
        return;
    }

    pEmitterMeshData_->frequencyTime += Frame::DeltaTime();
    if (pEmitterMeshData_->frequency <= pEmitterMeshData_->frequencyTime)
    {
        pEmitterMeshData_->frequencyTime -= pEmitterMeshData_->frequency;
        pEmitterMeshData_->emit = 1;
    }
    else
    {
        pEmitterMeshData_->emit = 0;
    }
}
void ParticleCSEmitter::CreateEmitterMeshResource()
{
    emitterMeshResource_ = pDxCommon_->CreateBufferResource(sizeof(EmitterMesh));
    emitterMeshResource_->Map(0, nullptr, reinterpret_cast<void **>(&pEmitterMeshData_));
    pEmitterMeshData_->frequency = 0.5f;
    pEmitterMeshData_->frequencyTime = 0.0f;
    pEmitterMeshData_->translate = Vector3(0.0f, 0.0f, 0.0f);
    pEmitterMeshData_->rotation = Quaternion::IdentityQuaternion();
    pEmitterMeshData_->scale = Vector3(1.0f, 1.0f, 1.0f);
    pEmitterMeshData_->triangleCount = 0;
    pEmitterMeshData_->emit = 0;
    pEmitterMeshData_->emitFromSurface = 1;
    pEmitterMeshData_->edgeCount = 0;
    pEmitterMeshData_->anchorPoint = Vector3(0.5f, 0.5f, 0.5f);
    pEmitterMeshData_->emitCountOverride = 0;
}

void ParticleCSEmitter::EmitterDisPatch(ID3D12GraphicsCommandList *pCommandList)
{
    // 引数が省略された場合はエミッタ保持のコマンドリストを使う
    if (pCommandList == nullptr)
    {
        pCommandList = pCommandList_;
    }
    ComputePipelineManager::GetInstance()->DrawCommonSetting(
        ComputePipelineType::Emitter,
        BlendMode::Normal, ShaderMode::None, pCommandList);

    // フィールド接触Emitモードの発生数は「対象フィールドの今フレームのバースト数合計」。
    // 各フィールドの data.emitSpawnCount には ParticleCSFieldManager::Update() が
    // 間隔タイマーから算出した今フレームの値が入っている（バースト無しフレームは0）。
    // シェーダはこの合計スレッドを累積和でフィールドごとに配分する（FindEmitTargetField）。
    // 通常モードは 0 にしてグループ設定 gSettings.emitCount を使わせる。
    uint32_t fieldBurstTotal = 0;
    if (emitOnlyOnFieldContact_ && receiveFields_)
    {
        for (const auto &field : ParticleCSFieldManager::GetInstance()->GetFields())
        {
            if (!field.enabled || !field.data.enableEmitSpawn)
                continue;
            bool groupMatch = (field.data.groupId == -1) ||
                              (fieldGroupId_ == -1) ||
                              (field.data.groupId == fieldGroupId_);
            if (groupMatch)
            {
                fieldBurstTotal += field.data.emitSpawnCount;
            }
        }
    }
    pEmitterMeshData_->emitCountOverride = fieldBurstTotal;

    for (uint32_t groupIndex = 0; groupIndex < particleGroups_.size(); ++groupIndex)
    {
        auto &group = particleGroups_[groupIndex];
        // アイドルグループ(生存0かつ発生なし。DrawCompute で判定)は発生ディスパッチも省く。
        if (groupIndex < groupActive_.size() && !groupActive_[groupIndex])
            continue;
        group->GetPerFrameData()->groupId = groupIndex;
        group->GetPerFrameData()->emitterFieldGroupId = fieldGroupId_;

        ParticleCSSettings *settings = group->GetSettingsData();

        // 渦の軸と目標オフセットを「基準空間」からワールドへ解決してから GPU へ渡す。
        // effectSpace=0(ワールド) では単位行列なので従来と完全に同じ値になる。
        // 1(エミッター)/2(ビルボード) では軸もオフセットも一緒に回るため、
        // エミッターやカメラを回しても渦の見え方（＝動き）が変わらない。
        const Matrix4x4 space = GetEffectSpaceMatrix(settings->effectSpace);
        settings->gatherTarget = pEmitterMeshData_->translate + TransformNormal(settings->gatherTargetOffset, space);
        settings->vortexTarget = pEmitterMeshData_->translate + TransformNormal(settings->vortexTargetOffset, space);
        settings->vortexAxis = TransformNormal(settings->vortexAxisBase, space);

        // SoA UAV (u0-u5)
        pCommandList->SetComputeRootDescriptorTable(0, group->GetLifeUavGpu());
        pCommandList->SetComputeRootDescriptorTable(1, group->GetDrawCoreUavGpu());
        pCommandList->SetComputeRootDescriptorTable(2, group->GetSimCoreUavGpu());
        pCommandList->SetComputeRootDescriptorTable(3, group->GetTrailUavGpu());
        pCommandList->SetComputeRootDescriptorTable(4, group->GetRotationUavGpu());
        pCommandList->SetComputeRootDescriptorTable(5, group->GetOverrideUavGpu());
        // フリーリスト (u6-u8)
        pCommandList->SetComputeRootDescriptorTable(6, group->GetFreeListIndexSrvHandle().second);
        pCommandList->SetComputeRootDescriptorTable(7, group->GetFreeListSrvHandle().second);
        pCommandList->SetComputeRootDescriptorTable(8, group->GetFreeListTrailIndexSrvHandle().second);
        // 生存リスト間接ディスパッチ (u9-u11): out リスト/カウンタ + renderCompact。
        //   Emit は新規粒子をここへ append する。continue より前に常時バインドしておく。
        pCommandList->SetComputeRootDescriptorTable(17, group->GetAliveListUavHandle().second);
        pCommandList->SetComputeRootDescriptorTable(18, group->GetAliveCounterUavHandle().second);
        pCommandList->SetComputeRootDescriptorTable(19, group->GetRenderCompactUavGpu());
        // 視錐台カリング後の描画リスト (u12:可視カウンタ / u13:描画順->slot index)
        pCommandList->SetComputeRootDescriptorTable(20, group->GetVisibleCounterUavHandle().second);
        pCommandList->SetComputeRootDescriptorTable(21, group->GetRenderSlotUavGpu());
        // CBV (b0-b2)。b3:FieldCB はフィールド有無で下の分岐が param 12 に設定する。
        pCommandList->SetComputeRootConstantBufferView(9, emitterMeshResource_->GetGPUVirtualAddress());
        pCommandList->SetComputeRootConstantBufferView(10, group->GetPerFrameResource()->GetGPUVirtualAddress());
        pCommandList->SetComputeRootConstantBufferView(11, group->GetSettingsResource()->GetGPUVirtualAddress());

        if (pEmitterMeshData_->triangleCount > 0 && triangleInfoResource_ && triangleCDFResource_)
        {
            pCommandList->SetComputeRootDescriptorTable(13, triangleInfoSrvHandle_.second);
            pCommandList->SetComputeRootDescriptorTable(14, triangleCDFSrvHandle_.second);
        }

        if (pEmitterMeshData_->edgeCount > 0 && edgeInfoResource_)
        {
            pCommandList->SetComputeRootDescriptorTable(15, edgeInfoSrvHandle_.second);
        }

        {
            auto *fieldManager = ParticleCSFieldManager::GetInstance();
            pCommandList->SetComputeRootDescriptorTable(16, fieldManager->GetFieldsSrvHandle().second);

            if (emitOnlyOnFieldContact_ && receiveFields_)
            {
                // 今フレームのバーストが無ければディスパッチ自体を省く
                // （対象フィールド不在・間隔待ちの両方をカバー）
                if (fieldBurstTotal == 0)
                {
                    pCommandList->SetComputeRootConstantBufferView(12, fieldManager->GetZeroFieldCountResource()->GetGPUVirtualAddress());
                    continue;
                }

                // 発生数は pEmitterMeshData_->emitCountOverride(=バースト合計) 経由でシェーダへ渡す。
                // 各スレッドの担当フィールドはシェーダが emitSpawnCount の累積和で決める。
                // 寿命は担当フィールドの emitSpawnLifeTime をシェーダ側が per-field で上書きする。
                pCommandList->SetComputeRootConstantBufferView(12, fieldManager->GetFieldCountResource()->GetGPUVirtualAddress());

                int dispatchCount = (static_cast<int>(fieldBurstTotal) + threadGroupSize_ - 1) / threadGroupSize_;
                pCommandList->Dispatch(dispatchCount, 1, 1);

                continue;
            }
            else
            {
                pCommandList->SetComputeRootConstantBufferView(12, fieldManager->GetZeroFieldCountResource()->GetGPUVirtualAddress());
            }
        }

        if (pEmitterMeshData_->emit == 0 || settings->emitCount == 0)
        {
            continue;
        }

        int dispatchCount = (group->GetSettingsData()->emitCount + threadGroupSize_ - 1) / threadGroupSize_;
        pCommandList->Dispatch(dispatchCount, 1, 1);
    }
}

void ParticleCSEmitter::CreateModelTriangles()
{
    if (modelData_.meshes.empty())
        return;

    triangleInfoList_.clear();
    triangleCDF_.clear();
    std::vector<float> triangleAreas;

    for (const auto &mesh : modelData_.meshes)
    {
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            uint32_t i0 = mesh.indices[i];
            uint32_t i1 = mesh.indices[i + 1];
            uint32_t i2 = mesh.indices[i + 2];

            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                continue;

            Vector3 v0(mesh.vertices[i0].position.x, mesh.vertices[i0].position.y, mesh.vertices[i0].position.z);
            Vector3 v1(mesh.vertices[i1].position.x, mesh.vertices[i1].position.y, mesh.vertices[i1].position.z);
            Vector3 v2(mesh.vertices[i2].position.x, mesh.vertices[i2].position.y, mesh.vertices[i2].position.z);

            Vector3 edge1 = v1 - v0;
            Vector3 edge2 = v2 - v0;
            Vector3 crossProd = edge1.Cross(edge2);
            float area = crossProd.Length() * 0.5f;

            if (area > 1e-6f)
            {
                triangleAreas.push_back(area);

                TriangleInfo triInfo;
                triInfo.v0 = v0;
                triInfo.v1 = v1;
                triInfo.v2 = v2;
                triInfo.padding0 = 0.0f;
                triInfo.padding1 = 0.0f;
                triInfo.padding2 = 0.0f;

                triangleInfoList_.push_back(triInfo);
            }
        }
    }

    if (triangleInfoList_.empty())
        return;

    std::vector<size_t> indices(triangleInfoList_.size());
    for (size_t i = 0; i < indices.size(); i++)
    {
        indices[i] = i;
    }

    float totalArea = 0.0f;
    for (float area : triangleAreas)
    {
        totalArea += area;
    }

    triangleCDF_.resize(triangleAreas.size());
    float accum = 0.0f;
    for (size_t i = 0; i < triangleAreas.size(); i++)
    {
        accum += triangleAreas[i] / totalArea;
        triangleCDF_[i] = accum;
    }

    // 最後の値を強制的に1.0にして誤差を修正
    if (!triangleCDF_.empty())
    {
        triangleCDF_.back() = 1.0f;
    }

    int histogram[10] = {0};
    for (float cdf : triangleCDF_)
    {
        int bucket = static_cast<int>(cdf * 10.0f);
        if (bucket >= 10)
            bucket = 9;
        histogram[bucket]++;
    }

    size_t triangleInfoBufferSize = sizeof(TriangleInfo) * triangleInfoList_.size();
    triangleInfoResource_ = pDxCommon_->CreateBufferResource(triangleInfoBufferSize);
    triangleInfoResource_->Map(0, nullptr, reinterpret_cast<void **>(&pTriangleInfoData_));
    std::memcpy(pTriangleInfoData_, triangleInfoList_.data(), triangleInfoBufferSize);

    triangleInfoSrvIndex_ = pSrvManager_->Allocate() + 1;
    triangleInfoSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(triangleInfoSrvIndex_);
    triangleInfoSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(triangleInfoSrvIndex_);
    pSrvManager_->CreateSRVforStructuredBuffer(triangleInfoSrvIndex_, triangleInfoResource_.Get(),
                                              static_cast<uint32_t>(triangleInfoList_.size()), sizeof(TriangleInfo));

    size_t cdfBufferSize = sizeof(float) * triangleCDF_.size();
    triangleCDFResource_ = pDxCommon_->CreateBufferResource(cdfBufferSize);
    triangleCDFResource_->Map(0, nullptr, reinterpret_cast<void **>(&pTriangleCDFData_));
    std::memcpy(pTriangleCDFData_, triangleCDF_.data(), cdfBufferSize);

    triangleCDFSrvIndex_ = pSrvManager_->Allocate() + 1;
    triangleCDFSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(triangleCDFSrvIndex_);
    triangleCDFSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(triangleCDFSrvIndex_);
    pSrvManager_->CreateSRVforStructuredBuffer(triangleCDFSrvIndex_, triangleCDFResource_.Get(),
                                              static_cast<uint32_t>(triangleCDF_.size()), sizeof(float));

    pEmitterMeshData_->triangleCount = static_cast<uint32_t>(triangleInfoList_.size());
}

void ParticleCSEmitter::CreateModelEdges()
{
    edgeInfoList_.clear();

    // Cylinder プリミティブは三角形由来のエッジ（対角線が混じる）ではなく、
    // 縦線＋横リングだけの綺麗な格子線を解析的に生成する（エッジ発生モードで籠状になる）。
    if (modelPath_.empty() && primitiveType_ == PrimitiveType::Cylinder)
    {
        auto lines = PrimitiveModel::GetInstance()->BuildCylinderGridLines(
            primitiveParams_.divide, primitiveParams_.heightDivide);
        edgeInfoList_.reserve(lines.size());
        for (const auto &seg : lines)
        {
            EdgeInfo edgeInfo;
            edgeInfo.v0 = seg.first;
            edgeInfo.v1 = seg.second;
            edgeInfo.padding0 = 0.0f;
            edgeInfo.padding1 = 0.0f;
            edgeInfoList_.push_back(edgeInfo);
        }
    }
    else
    {
        if (modelData_.meshes.empty())
            return;

        std::map<std::pair<uint32_t, uint32_t>, int> edgeMap;

        for (const auto &mesh : modelData_.meshes)
        {
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            {
                uint32_t i0 = mesh.indices[i];
                uint32_t i1 = mesh.indices[i + 1];
                uint32_t i2 = mesh.indices[i + 2];

                if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                    continue;

                std::array<std::pair<uint32_t, uint32_t>, 3> edges = {{{std::min(i0, i1), std::max(i0, i1)},
                                                                       {std::min(i1, i2), std::max(i1, i2)},
                                                                       {std::min(i2, i0), std::max(i2, i0)}}};

                for (const auto &edge : edges)
                {
                    edgeMap[edge]++;
                }
            }
        }

        bool isClosedMesh = true;
        for (const auto &[edge, count] : edgeMap)
        {
            if (count == 1)
            {
                isClosedMesh = false;
                break;
            }
        }

        for (const auto &[edge, count] : edgeMap)
        {
            if (!isClosedMesh && count != 1)
                continue;

            uint32_t idx0 = edge.first;
            uint32_t idx1 = edge.second;

            Vector3 v0, v1;
            bool found = false;
            for (const auto &mesh : modelData_.meshes)
            {
                if (idx0 < mesh.vertices.size() && idx1 < mesh.vertices.size())
                {
                    v0 = Vector3(mesh.vertices[idx0].position.x,
                                 mesh.vertices[idx0].position.y,
                                 mesh.vertices[idx0].position.z);
                    v1 = Vector3(mesh.vertices[idx1].position.x,
                                 mesh.vertices[idx1].position.y,
                                 mesh.vertices[idx1].position.z);
                    found = true;
                    break;
                }
            }

            if (found)
            {
                EdgeInfo edgeInfo;
                edgeInfo.v0 = v0;
                edgeInfo.v1 = v1;
                edgeInfo.padding0 = 0.0f;
                edgeInfo.padding1 = 0.0f;
                edgeInfoList_.push_back(edgeInfo);
            }
        }
    }

    if (edgeInfoList_.empty())
        return;

    size_t edgeInfoBufferSize = sizeof(EdgeInfo) * edgeInfoList_.size();
    edgeInfoResource_ = pDxCommon_->CreateBufferResource(edgeInfoBufferSize);
    edgeInfoResource_->Map(0, nullptr, reinterpret_cast<void **>(&pEdgeInfoData_));
    std::memcpy(pEdgeInfoData_, edgeInfoList_.data(), edgeInfoBufferSize);

    edgeInfoSrvIndex_ = pSrvManager_->Allocate() + 1;
    edgeInfoSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(edgeInfoSrvIndex_);
    edgeInfoSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(edgeInfoSrvIndex_);
    pSrvManager_->CreateSRVforStructuredBuffer(edgeInfoSrvIndex_, edgeInfoResource_.Get(),
                                              static_cast<uint32_t>(edgeInfoList_.size()), sizeof(EdgeInfo));

    pEmitterMeshData_->edgeCount = static_cast<uint32_t>(edgeInfoList_.size());
}

size_t ParticleCSEmitter::GetTotalAliveParticles()
{
    size_t total = 0;
    for (auto &group : particleGroups_)
    {
        total += group->GetAliveParticleCount();
    }
    return total;
}

size_t ParticleCSEmitter::GetSceneAliveParticleCount()
{
    // liveEmitters_ は生成/破棄で自動的に出入りするので、エディタ登録・Spawner 生成・
    // ゲームクラス所有のどれでも漏れなく数えられる。
    size_t total = 0;
    for (ParticleCSEmitter *pEmitter : liveEmitters_)
    {
        if (!pEmitter || pEmitter->previewOnly_)
            continue; // プレビュー窓専用はゲーム画面に出ていないので数えない
        total += pEmitter->GetTotalAliveParticles();
    }
    return total;
}

size_t ParticleCSEmitter::GetAllAliveParticleCount()
{
    size_t total = 0;
    for (ParticleCSEmitter *pEmitter : liveEmitters_)
    {
        if (!pEmitter)
            continue;
        total += pEmitter->GetTotalAliveParticles();
    }
    return total;
}

std::vector<ParticleCSEmitter::EmitterStatistics> ParticleCSEmitter::GetAllEmitterStatistics(bool includePreviewOnly)
{
    std::vector<EmitterStatistics> stats;
    stats.reserve(liveEmitters_.size());
    for (ParticleCSEmitter *pEmitter : liveEmitters_)
    {
        if (!pEmitter)
            continue;
        if (!includePreviewOnly && pEmitter->previewOnly_)
            continue;
        EmitterStatistics stat;
        stat.emitterName = pEmitter->GetName();
        stat.aliveCount = pEmitter->GetTotalAliveParticles();
        stat.previewOnly = pEmitter->previewOnly_;
        stats.push_back(std::move(stat));
    }
    return stats;
}

std::vector<ParticleCSEmitter::GroupStatistics> ParticleCSEmitter::GetGroupStatistics()
{
    std::vector<GroupStatistics> stats;

    for (auto &group : particleGroups_)
    {
        GroupStatistics stat;
        stat.groupName = group->GetGroupName();
        stat.aliveCount = group->GetAliveParticleCount();
        stats.push_back(stat);
    }

    return stats;
}

} // namespace Hagine
