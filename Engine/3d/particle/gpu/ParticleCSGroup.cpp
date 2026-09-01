#define NOMINMAX
#include "ParticleCSGroup.h"
#include <audio/Audio.h>
#include <algorithm>
#include <cmath>
#include <Frame.h>
#include <graphics/model/ModelManager.h>
#include <graphics/pipeline/ComputePipelineManager.h>
#include <d3dx12.h>

// GPU リソースの生成・ディスパッチ・読み戻しの本体。
// エディタUI（DrawImGui とその補助）は ParticleCSGroupImGui.cpp にある。
namespace Hagine {
void ParticleCSGroup::Initialize(uint32_t maxParticleCount)
{
    pDxCommon_ = ParticleCommon::GetInstance()->GetDxCommon();
    pSrvManager_ = SrvManager::GetInstance();
    particleCommon_ = ParticleCommon::GetInstance();
    pTextureManager_ = TextureManager::GetInstance();
    pCommandList_ = pDxCommon_->GetCommandList().Get();
    computeCommandList_ = pDxCommon_->GetComputeCommandList().Get();
    CreateSettingsResource();
    pSettingsData_->maxParticleCount = maxParticleCount;
    CreateParticleSoABuffers();
    CreatePerViewResource();
    CreatePerFrameResource();
    CreateFreeListIndexResource();
    CreateFreeListTrailIndexResource();
    CreateFreeListResource();
    CreateAliveCountResource();
    CreateAliveListResources();

    pPerViewData_->enableBillboard = 1;

    isInitialized_ = true;
}

int ParticleCSGroup::CalculateOptimalEmitCount() const
{
    if (frequency_ <= 0.0f || pSettingsData_->lifeTimeMax <= 0.0f)
    {
        return static_cast<int>(pSettingsData_->maxParticleCount);
    }

    float emissionCount = pSettingsData_->lifeTimeMax / frequency_;

    int result;
    if (emissionCount <= 1.0f)
    {
        result = static_cast<int>(pSettingsData_->maxParticleCount);
    }
    else
    {
        result = static_cast<int>(pSettingsData_->maxParticleCount / emissionCount);
    }

    return std::clamp(result, 1, static_cast<int>(pSettingsData_->maxParticleCount));
}

ParticleCSGroup::~ParticleCSGroup()
{
    if (!isInitialized_)
    {
        return;
    }

    // Map済みリソースのUnmap
    if (settingsResource_)
    {
        settingsResource_->Unmap(0, nullptr);
    }
    if (perViewResource_)
    {
        perViewResource_->Unmap(0, nullptr);
    }
    if (perFrameResource_)
    {
        perFrameResource_->Unmap(0, nullptr);
    }
    if (materialResource_)
    {
        materialResource_->Unmap(0, nullptr);
    }
    if (vertexResource_)
    {
        vertexResource_->Unmap(0, nullptr);
    }
    if (indexResource_)
    {
        indexResource_->Unmap(0, nullptr);
    }
}

ParticleCSGroupData ParticleCSGroup::CreateParticleGroup(const std::string &groupName, const std::string &filename, uint32_t maxParticleCount, const std::string &texturePath, BlendMode blendMode)
{
    Initialize(maxParticleCount);
    particleGroupData_.groupName = groupName;
    modelFilePath_ = filename;
    ModelManager::GetInstance()->LoadModel(filename);
    pModel_ = ModelManager::GetInstance()->FindModel(filename);
    modelData_ = pModel_->GetModelData();
    CreateVertexResource();
    CreateIndexResource();
    CreateDrawArgsResources(); // GPU駆動描画(ExecuteIndirect)の引数バッファ（メッシュ確定後）
    // マテリアルが複数ある場合は最初のものを使う
    particleGroupData_.materials.clear();
    if (texturePath.empty())
    {
        if (!modelData_.materials.empty())
        {
            particleGroupData_.materials = ForParticleMaterials(modelData_.materials);
        }
        else
        {
            particleGroupData_.materials.push_back(ParticleMaterial{});
        }
    }
    else
    {
        ParticleMaterial mat;
        mat.textureFilePath = texturePath;
        mat.textureIndex = pTextureManager_->GetTextureIndexByFilePath(texturePath);
        particleGroupData_.materials.push_back(mat);
    }
    // すべてのマテリアルのテクスチャをロード
    for (auto &mat : particleGroupData_.materials)
    {
        pTextureManager_->LoadTexture(mat.textureFilePath);
        mat.textureIndex = pTextureManager_->GetTextureIndexByFilePath(mat.textureFilePath);
    }

    CreateMaterialResource();

    InitParticle();
    particleGroupData_.blendMode = blendMode;
    return particleGroupData_;
}

ParticleCSGroupData ParticleCSGroup::CreatePrimitiveParticleGroup(const std::string &groupName, PrimitiveType type, uint32_t maxParticleCount, const std::string &texturePath, BlendMode blendMode)
{
    Initialize(maxParticleCount);
    particleGroupData_.groupName = groupName;
    type_ = type;
    pModel_ = ModelManager::GetInstance()->FindModel(ModelManager::GetInstance()->CreatePrimitiveModel(type, texturePath));
    pTextureManager_->LoadTexture(texturePath);
    modelData_ = pModel_->GetModelData();
    CreateVertexResource();
    CreateIndexResource();
    CreateDrawArgsResources(); // GPU駆動描画(ExecuteIndirect)の引数バッファ（メッシュ確定後）
    // マテリアルが複数ある場合は最初のものを使う
    particleGroupData_.materials.clear();
    if (texturePath.empty())
    {
        if (!modelData_.materials.empty())
        {
            particleGroupData_.materials = ForParticleMaterials(modelData_.materials);
        }
        else
        {
            particleGroupData_.materials.push_back(ParticleMaterial{});
        }
    }
    else
    {
        ParticleMaterial mat;
        mat.textureFilePath = texturePath;
        mat.textureIndex = pTextureManager_->GetTextureIndexByFilePath(texturePath);
        particleGroupData_.materials.push_back(mat);
    }
    // すべてのマテリアルのテクスチャをロード
    for (auto &mat : particleGroupData_.materials)
    {
        pTextureManager_->LoadTexture(mat.textureFilePath);
        mat.textureIndex = pTextureManager_->GetTextureIndexByFilePath(mat.textureFilePath);
    }

    CreateMaterialResource();

    InitParticle();
    particleGroupData_.blendMode = blendMode;

    return particleGroupData_;
}

void ParticleCSGroup::SetTexture(const std::string &path)
{
    if (path.empty() || particleGroupData_.materials.empty())
        return;
    pTextureManager_->LoadTexture(path);
    uint32_t index = pTextureManager_->GetTextureIndexByFilePath(path);
    // 描画は毎フレーム textureFilePath で SRV を引くのでパス差し替えで即時反映される。
    // textureIndex も一応更新しておく。
    for (auto &m : particleGroupData_.materials)
    {
        m.textureFilePath = path;
        m.textureIndex = index;
    }
}

void ParticleCSGroup::InitParticle()
{
    pSrvManager_->SetDescriptorHeap();

    pDxCommon_->TransitionUAVBarrier(soaLife_.resource.Get());

    // InitParticle.CS: SoA は Life バッファ(u0)のみ初期化すればよい
    particleCommon_->ComputeInitDrawCommonSetting();
    pCommandList_->SetComputeRootDescriptorTable(0, soaLife_.uavHandle.second);
    pCommandList_->SetComputeRootDescriptorTable(1, freeListIndexSrvHandle_.second);
    pCommandList_->SetComputeRootDescriptorTable(2, freeListSrvHandle_.second);
    pCommandList_->SetComputeRootDescriptorTable(3, freeListTrailIndexSrvHandle_.second);
    pCommandList_->SetComputeRootConstantBufferView(4, settingsResource_->GetGPUVirtualAddress());
    int disPatchCount = (pSettingsData_->maxParticleCount + threadsPerGroup_ - 1) / threadsPerGroup_;
    pCommandList_->Dispatch(disPatchCount, 1, 1);

    pDxCommon_->TransitionSRVBarrier();
}

bool ParticleCSGroup::CanUseLiteUpdate(bool fieldsActive) const
{
    // フィールドの影響を受けるグループはフル版必須（force-trail/override/colorMul 等）。
    if (fieldsActive)
        return false;
    const ParticleCSSettings *s = pSettingsData_;
    // 軽量版が持たない重い演出が1つでも有効ならフル版を使う。
    if (s->enableTrail != 0)
        return false;
    if (s->enableGather != 0)
        return false;
    if (s->enableVortex != 0)
        return false;
    if (s->enableCurlNoise != 0)
        return false;
    if (s->enableTurbulence != 0)
        return false;
    if (s->enableAudioVibration != 0)
        return false;
    if (s->enableRandomRotation != 0 || s->enableRandomAngularVelocity != 0)
        return false;
    return true;
}

void ParticleCSGroup::UpdateParticleCSDisPatch(
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> fieldsSrvHandle,
    Microsoft::WRL::ComPtr<ID3D12Resource> fieldCountResource,
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> overrideSrvHandle,
    bool fieldsActive,
    ID3D12GraphicsCommandList *pCommandList)
{
    // フル版が Trail/Rotation/Override を触る場合のみ、ここで本確保へ作り直す
    // （演出なしグループは 1要素ダミーのままで VRAM を節約）。バインドより前に行う。
    EnsureUpdateOptionalBuffers(fieldsActive);
    // pCommandList が渡された場合はそちら（非同期 Compute Queue）を使う
    // 引数が省略された場合はグループ保持のコマンドリストを使う
    if (pCommandList == nullptr)
    {
        pCommandList = pCommandList_;
    }
    auto *computePSOMgr = ComputePipelineManager::GetInstance();
    // 演出なしグループは軽量 PSO を使う。root sig はフル版と共有なので
    // バインド（下記 17 パラメータ）は両者で同一。軽量シェーダは未使用の
    // テーブルを無視するだけで安全。
    const ComputePipelineType updateType = CanUseLiteUpdate(fieldsActive)
                                               ? ComputePipelineType::UpdateEmitterLite
                                               : ComputePipelineType::UpdateEmitter;
    computePSOMgr->DrawCommonSetting(updateType,
                                     BlendMode::Normal, ShaderMode::None, pCommandList);
    // SoA UAV (u0-u5)
    pCommandList->SetComputeRootDescriptorTable(0, soaLife_.uavHandle.second);
    pCommandList->SetComputeRootDescriptorTable(1, soaDrawCore_.uavHandle.second);
    pCommandList->SetComputeRootDescriptorTable(2, soaSimCore_.uavHandle.second);
    pCommandList->SetComputeRootDescriptorTable(3, soaTrail_.uavHandle.second);
    pCommandList->SetComputeRootDescriptorTable(4, soaRotation_.uavHandle.second);
    pCommandList->SetComputeRootDescriptorTable(5, soaOverride_.uavHandle.second);
    // フリーリスト (u6-u8)
    pCommandList->SetComputeRootDescriptorTable(6, freeListIndexSrvHandle_.second);
    pCommandList->SetComputeRootDescriptorTable(7, freeListSrvHandle_.second);
    pCommandList->SetComputeRootDescriptorTable(8, freeListTrailIndexSrvHandle_.second);
    // 生存コンパクション (u9-u10): out フェーズへ書き出す
    pCommandList->SetComputeRootDescriptorTable(9, aliveListUavHandle_[alivePhase_].second);
    pCommandList->SetComputeRootDescriptorTable(10, aliveCounterUavHandle_[alivePhase_].second);
    // 描画コンパクション (u11)
    pCommandList->SetComputeRootDescriptorTable(11, soaRenderCompact_.uavHandle.second);
    // CBV (b0-b2) / SRV (t0-t1)
    pCommandList->SetComputeRootConstantBufferView(12, perFrameResource_->GetGPUVirtualAddress());
    pCommandList->SetComputeRootConstantBufferView(13, settingsResource_->GetGPUVirtualAddress());
    pCommandList->SetComputeRootConstantBufferView(14, fieldCountResource->GetGPUVirtualAddress());
    pCommandList->SetComputeRootDescriptorTable(15, fieldsSrvHandle.second);
    pCommandList->SetComputeRootDescriptorTable(16, overrideSrvHandle.second);
    // 生存リスト間接ディスパッチ (t2,t3): in リスト/カウンタ = 前フレームの out フェーズ
    const uint32_t inIdx = alivePhase_ ^ 1u;
    pCommandList->SetComputeRootDescriptorTable(17, pSrvManager_->GetGPUDescriptorHandle(aliveListSrvForVSIndex_[inIdx]));
    pCommandList->SetComputeRootDescriptorTable(18, pSrvManager_->GetGPUDescriptorHandle(aliveCounterSrvForVSIndex_[inIdx]));
    // 視錐台カリング後の描画リスト (u12:可視カウンタ / u13:描画順->slot index)
    pCommandList->SetComputeRootDescriptorTable(19, visibleCounterUavHandle_.second);
    pCommandList->SetComputeRootDescriptorTable(20, soaRenderSlot_.uavHandle.second);

    // 軽量版・フル版ともスレッドグループ256（Ampere の常駐1536上限で占有率を上げる狙い）。
    // 各シェーダの [numthreads] と一致必須（Lite=UpdateParticleLite / Full=UpdateParticle）。
    const uint32_t groupSize = (updateType == ComputePipelineType::UpdateEmitterLite)
                                   ? kLiteUpdateThreadsPerGroup
                                   : kFullUpdateThreadsPerGroup;

    // 生存リスト間接ディスパッチ Step3: dispatch 本数を「in リスト長」由来にして O(生存数) 化する。
    //   in リスト = 前フレームの out リスト。その長さは out カウンタの readback 値(aliveDrawCount_,
    //   1〜2F 遅延)で近似する。最新値を取り込んでから使う。
    //   GPU 側は `tid >= gAliveCounterIn[0]` で多い分を捨てるので over-dispatch は無害。
    //   ★逆に in リスト長より少なく dispatch すると未処理粒子が out に積まれず、その slot が
    //     漏れる（描画の取りこぼしと違い自己回収しない）。readback 遅延中の成長(新規Emit/
    //     トレイル子)を取りこぼさないよう margin（25% + emitCount + 定数）を安全側に上乗せし、
    //     maxParticleCount でクランプする。これで疎なら数千万 MAX でも Update が ~0.1ms に近づく。
    FetchAliveDrawCount();
    const uint32_t maxCount = pSettingsData_->maxParticleCount;
    uint32_t inLenEst = aliveDrawCount_;
    if (inLenEst > maxCount)
        inLenEst = maxCount; // 初回フレーム等の未初期化/異常値ガード（オーバーフロー防止）
    uint32_t threadCount = inLenEst + inLenEst / 4u + pSettingsData_->emitCount + 4096u;
    if (threadCount > maxCount)
        threadCount = maxCount;
    int disPatchCount = (threadCount + groupSize - 1) / groupSize;
    if (disPatchCount < 1)
        disPatchCount = 1;
    pCommandList->Dispatch(disPatchCount, 1, 1);
}

void ParticleCSGroup::ResetAliveCounterDispatch(ID3D12GraphicsCommandList *pCommandList)
{
    // 引数が省略された場合はグループ保持のコマンドリストを使う
    if (pCommandList == nullptr)
    {
        pCommandList = pCommandList_;
    }
    ComputePipelineManager::GetInstance()->DrawCommonSetting(
        ComputePipelineType::ResetArgs, BlendMode::Normal, ShaderMode::None, pCommandList);
    // out フェーズのカウンタを 0 にリセットする。
    pCommandList->SetComputeRootDescriptorTable(0, aliveCounterUavHandle_[alivePhase_].second);
    pCommandList->Dispatch(1, 1, 1);
    // 描画リスト(視錐台カリング後)のカウンタも同じパスで 0 に戻す。
    pCommandList->SetComputeRootDescriptorTable(0, visibleCounterUavHandle_.second);
    pCommandList->Dispatch(1, 1, 1);

    // リセット完了を Emit/Update の InterlockedAdd より前に保証する
    D3D12_RESOURCE_BARRIER uavBarriers[2] = {};
    uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[0].UAV.pResource = aliveCounterResource_[alivePhase_].Get();
    uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[1].UAV.pResource = visibleCounterResource_.Get();
    pCommandList->ResourceBarrier(2, uavBarriers);
}

void ParticleCSGroup::RecordAliveCountReadback(ID3D12GraphicsCommandList *computeCmdList)
{
    // Update が compute queue で書いた直後（バッファが UAV 状態）にコピーする。
    // この時点でカウンタは UnorderedAccess へ昇格済みなので状態遷移は整合する。
    ID3D12GraphicsCommandList *pCommandList = computeCmdList ? computeCmdList : pCommandList_;

    // out フェーズのカウンタを読み戻す（共有 readback へコピー）。
    ID3D12Resource *counterRes = aliveCounterResource_[alivePhase_].Get();

    // Update の append 完了を保証
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = counterRes;
    pCommandList->ResourceBarrier(1, &uavBarrier);

    auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        counterRes,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    pCommandList->ResourceBarrier(1, &toCopy);

    pCommandList->CopyResource(aliveCounterReadbackResource_.Get(), counterRes);

    auto toUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        counterRes,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    pCommandList->ResourceBarrier(1, &toUAV);

    // ---- GPU駆動描画: 引数バッファの InstanceCount を GPU カウンタで上書きする ----
    //   コピー元は「描画リスト長＝視錐台カリングを通った数」。CPU 読み戻し値
    //   （1〜2F遅延＋マージン）を使わないので、画面外の粒子ぶんの頂点シェーダも起動しない。
    //   drawArgsResource_ は COMMON のままにして COPY_DEST への暗黙昇格に任せる
    //   （バッファは ExecuteCommandLists 完了で COMMON へ減衰するので、direct キューでは
    //     INDIRECT_ARGUMENT へ昇格して ExecuteIndirect が読める）。
    if (drawArgsResource_)
    {
        ID3D12Resource *visibleRes = visibleCounterResource_.Get();
        // Emit/Update の append 完了を保証してからコピー状態へ移す
        D3D12_RESOURCE_BARRIER visUavBarrier{};
        visUavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        visUavBarrier.UAV.pResource = visibleRes;
        pCommandList->ResourceBarrier(1, &visUavBarrier);

        auto visToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
            visibleRes,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        pCommandList->ResourceBarrier(1, &visToCopy);

        // 固定部（IndexCountPerInstance / Start*）は初回だけコピーする。
        // InstanceCount(オフセット+4) を跨がない2区間に分けるので、下のカウンタコピーと
        // 書き込み範囲が重ならない（コピー同士の順序に依存しない）。
        if (!drawArgsStaticUploaded_)
        {
            for (uint32_t i = 0; i < drawArgsCount_; ++i)
            {
                const UINT64 base = static_cast<UINT64>(kDrawArgsStride) * i;
                pCommandList->CopyBufferRegion(drawArgsResource_.Get(), base,
                                               drawArgsUploadResource_.Get(), base,
                                               sizeof(uint32_t)); // IndexCountPerInstance
                pCommandList->CopyBufferRegion(drawArgsResource_.Get(), base + sizeof(uint32_t) * 2,
                                               drawArgsUploadResource_.Get(), base + sizeof(uint32_t) * 2,
                                               sizeof(uint32_t) * 3); // Start*/BaseVertex
            }
            drawArgsStaticUploaded_ = true;
        }
        for (uint32_t i = 0; i < drawArgsCount_; ++i)
        {
            const UINT64 dstOffset = static_cast<UINT64>(kDrawArgsStride) * i + sizeof(uint32_t);
            pCommandList->CopyBufferRegion(drawArgsResource_.Get(), dstOffset,
                                           visibleRes, 0, sizeof(uint32_t)); // InstanceCount
        }
        drawArgsReady_ = true;

        auto visToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
            visibleRes,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        pCommandList->ResourceBarrier(1, &visToUAV);
    }
}

void ParticleCSGroup::FetchAliveDrawCount()
{
    // 直近フレームにコピー済みの値を読み取る（1〜2フレーム遅延・許容）
    uint32_t *mappedData = nullptr;
    D3D12_RANGE readRange{0, sizeof(uint32_t)};
    HRESULT hr = aliveCounterReadbackResource_->Map(0, &readRange, reinterpret_cast<void **>(&mappedData));
    if (SUCCEEDED(hr) && mappedData)
    {
        aliveDrawCount_ = *mappedData;
        aliveCounterReadbackResource_->Unmap(0, nullptr);
    }
}

void ParticleCSGroup::Update(const ViewProjection &vp)
{
    pPerFrameData_->time += Frame::DeltaTime();
    pPerFrameData_->deltaTime = Frame::DeltaTime();

    // カラーグラデーション: ストップが変更された(dirty)なら 256段 LUT を CB へ再ベイクする。
    // 有効時のみベイク（OFF のグループは LUT を読まないので無駄を省く）。
    if (pSettingsData_->enableColorGradient != 0 && colorStopsDirty_)
    {
        BakeColorLUT();
        colorStopsDirty_ = false;
    }
    // 寿命カーブ(サイズ/アルファ)も同様に dirty 時のみ再ベイク（どちらか有効なら）。
    if ((pSettingsData_->enableSizeCurve != 0 || pSettingsData_->enableAlphaCurve != 0) && lifeCurvesDirty_)
    {
        BakeLifetimeCurveLUTs();
        lifeCurvesDirty_ = false;
    }

    // 音声振動: 有効なときだけ「音の立ち上がり(onset)」からエンベロープを作って CB に注入する。
    //   onset  = 今のピーク − 前フレームのピーク の正の部分（＝音が大きくなった“増加分”）。
    //   エンベロープ = 時間で指数減衰させつつ onset で即座に跳ね上げる（アタック即・リリース減衰）。
    //   → 波形が大きくなった瞬間にバンっと跳ね、その後スッと落ち着く（GPU が振動の駆動に使う）。
    // OFF のグループは触らない＝無回帰。Audio 参照は有効時のみで軽量（再生中ボイスの PCM をサンプルするだけ）。
    if (pSettingsData_->enableAudioVibration != 0)
    {
        const float peak = Audio::GetInstance()->GetCurrentAmplitude(); // [0,1] 現在のピーク
        const float dt = Frame::DeltaTime();
        const float onset = (std::max)(0.0f, peak - audioPrevPeak_); // 立ち上がり（増加分）
        audioPrevPeak_ = peak;
        // リリース: releaseRate[1/s] が大きいほど早く落ち着く（フレームレート非依存な指数減衰）
        const float releaseRate = (pSettingsData_->audioReleaseRate > 0.0f) ? pSettingsData_->audioReleaseRate : 10.0f;
        audioEnvelope_ *= std::exp(-releaseRate * dt);
        // アタック: onset の方が大きければ即座に跳ね上げる（＝バンっ）
        audioEnvelope_ = (std::max)(audioEnvelope_, onset);
        pSettingsData_->audioAmplitude = audioEnvelope_;
    }
    else
    {
        audioEnvelope_ = 0.0f;
        audioPrevPeak_ = 0.0f;
        pSettingsData_->audioAmplitude = 0.0f;
    }

    pPerViewData_->viewProjection = vp.matView_ * vp.matProjection_;
    // 距離カリング(overdraw 対策)用のカメラワールド座標。enableDistanceCull 等の設定値は
    // ImGui/ロードで設定された perView の値をそのまま保持する（Update では上書きしない）。
    pPerViewData_->cameraPosition = vp.translation_;
    // 画面サイズ上限/微小カリング用の射影スケール（projection[1][1] = cot(fovY/2)）。
    pPerViewData_->projScaleY = vp.matProjection_.m[1][1];
    // 回転を使わないグループは VS の回転行列計算（sincos×3＋行列積）を省くためのフラグ。
    pPerViewData_->enableRotation =
        (pSettingsData_->enableRandomRotation != 0 || pSettingsData_->enableRandomAngularVelocity != 0) ? 1u : 0u;
    if (pPerViewData_->enableBillboard)
    {
        pPerViewData_->billboardMatrix = vp.matView_;
        pPerViewData_->billboardMatrix.m[3][0] = 0.0f;
        pPerViewData_->billboardMatrix.m[3][1] = 0.0f;
        pPerViewData_->billboardMatrix.m[3][2] = 0.0f;
        pPerViewData_->billboardMatrix.m[3][3] = 1.0f;
        pPerViewData_->billboardMatrix = Inverse(pPerViewData_->billboardMatrix);
    }
    else
    {
        pPerViewData_->billboardMatrix = MakeIdentity4x4();
    }

    // ---- GPU駆動の視錐台カリング用パラメータ（Compute が読む定数バッファへ）----
    // 平面は「描画に使う viewProjection」から抽出するので、カリング判定と実際の
    // 射影結果が食い違わない。プレビュー窓は別カメラで同じバッファを描くため抑止する。
    pSettingsData_->enableFrustumCull =
        (!frustumCullSuppressed_ && frustumCullEnabled_) ? 1u : 0u;
    // 粒子半径 = max(scale) × モデル頂点の広がり（+ わずかな安全マージン）
    pSettingsData_->frustumRadiusScale = modelLocalRadius_ * 1.05f;
    // 速度ストレッチで伸びるぶんを半径に反映（OFF なら 0＝膨らませない）
    pSettingsData_->frustumStretchFactor =
        (pPerViewData_->enableVelocityStretch != 0) ? pPerViewData_->velocityStretchFactor : 0.0f;
    ExtractFrustumPlanes(pPerViewData_->viewProjection, pSettingsData_->frustumPlanes);

    CopyDebugDataToReadback();
}

void ParticleCSGroup::ExtractFrustumPlanes(const Matrix4x4 &viewProjection, Vector4 outPlanes[6])
{
    // 行ベクトル規約 (clip = v * VP) の Gribb/Hartmann 抽出。
    //   clip.x > -clip.w （左）… v・(col0 + col3) > 0 という具合に、列の和/差が平面になる。
    //   D3D の深度は 0〜1 なので near は col2 単体、far は col3 - col2。
    const auto &m = viewProjection.m;
    auto col = [&m](int c) {
        return Vector4(m[0][c], m[1][c], m[2][c], m[3][c]);
    };
    const Vector4 c0 = col(0);
    const Vector4 c1 = col(1);
    const Vector4 c2 = col(2);
    const Vector4 c3 = col(3);

    Vector4 planes[6] = {
        {c3.x + c0.x, c3.y + c0.y, c3.z + c0.z, c3.w + c0.w}, // left
        {c3.x - c0.x, c3.y - c0.y, c3.z - c0.z, c3.w - c0.w}, // right
        {c3.x + c1.x, c3.y + c1.y, c3.z + c1.z, c3.w + c1.w}, // bottom
        {c3.x - c1.x, c3.y - c1.y, c3.z - c1.z, c3.w - c1.w}, // top
        {c2.x, c2.y, c2.z, c2.w},                             // near
        {c3.x - c2.x, c3.y - c2.y, c3.z - c2.z, c3.w - c2.w}, // far
    };

    // 半径との比較を「距離」で行うために法線を単位化する
    for (int i = 0; i < 6; ++i)
    {
        const float len = std::sqrt(planes[i].x * planes[i].x + planes[i].y * planes[i].y + planes[i].z * planes[i].z);
        if (len > 1e-6f)
        {
            planes[i].x /= len;
            planes[i].y /= len;
            planes[i].z /= len;
            planes[i].w /= len;
        }
        outPlanes[i] = planes[i];
    }
}

void ParticleCSGroup::AllocateSoABuffer(SoABuffer &buf, uint32_t count)
{
    if (count == 0)
        count = 1;
    // 旧リソースは in-flight のコマンドリストが参照中の可能性があるため即解放しない。
    // 退避先へ移し、グループ破棄まで生かす（ダミーは要素1個なので極小）。
    if (buf.resource)
    {
        retiredSoABuffers_.push_back(buf.resource);
    }
    buf.resource = pDxCommon_->CreateBufferResource(static_cast<size_t>(buf.stride) * count, true);
    // 既存ディスクリプタ枠を上書きすると in-flight 参照とハザードになるため、
    // 毎回「新しい枠」を確保して作り直す（SrvManager は bump 割当なので枠は使い捨て）。
    buf.uavIndex = pSrvManager_->Allocate() + 1;
    buf.uavHandle.first = pSrvManager_->GetCPUDescriptorHandle(buf.uavIndex);
    buf.uavHandle.second = pSrvManager_->GetGPUDescriptorHandle(buf.uavIndex);
    pSrvManager_->CreateUAVStructuredBuffer(buf.uavIndex, buf.resource.Get(), count, buf.stride);
    if (buf.withSrvForVS)
    {
        buf.srvForVSIndex = pSrvManager_->Allocate() + 1;
        pSrvManager_->CreateSRVforStructuredBuffer(buf.srvForVSIndex, buf.resource.Get(), count, buf.stride);
    }
    buf.allocatedCount = count;
}

void ParticleCSGroup::CreateParticleSoABuffers()
{
    const uint32_t maxCount = pSettingsData_->maxParticleCount;

    auto initSoA = [&](SoABuffer &buf, uint32_t stride, bool withSrvForVS, uint32_t count) {
        buf.stride = stride;
        buf.withSrvForVS = withSrvForVS;
        AllocateSoABuffer(buf, count);
    };

    // 常時必要なバッファは maxCount で本確保。
    initSoA(soaLife_, sizeof(float), false, maxCount);
    initSoA(soaDrawCore_, sizeof(CSParticleDrawCore), false, maxCount); // sim専用(VSは描画コンパクションを読む)
    initSoA(soaSimCore_, sizeof(CSParticleSimCore), false, maxCount);
    // Trail/Rotation/Override は「使うグループだけ」後から本確保（演出なしは1要素ダミーのまま）。
    // → 演出なしグループの per-particle VRAM を 148B→96B(-35%) に削減し積める上限を引き上げる。
    //   EnsureUpdateOptionalBuffers が必要時に maxCount へ作り直す。
    initSoA(soaTrail_, sizeof(CSParticleTrail), false, 1);
    initSoA(soaRotation_, sizeof(CSParticleRotation), true, 1); // 描画VS t4(回転グループのみ)
    initSoA(soaOverride_, sizeof(CSParticleOverride), false, 1);
    // 描画コンパクション: 詰めた描画データ(DrawCore形式)。Update u11(UAV) / 描画VS t0(SRV)。常時必要。
    initSoA(soaRenderCompact_, sizeof(CSParticleDrawCore), true, maxCount);
    // 描画順 -> 実 slot index。回転グループだけが gRotation を引くのに使うので、
    // Trail/Rotation/Override と同じく「使うグループだけ」後から本確保する。
    initSoA(soaRenderSlot_, sizeof(uint32_t), true, 1); // 描画VS t2(回転グループのみ)
}

void ParticleCSGroup::EnsureUpdateOptionalBuffers(bool fieldsActive)
{
    const uint32_t maxCount = pSettingsData_->maxParticleCount;
    // フル版 Update のバッファ load/store ゲートと一致させる:
    //   useTrail    = enableTrail || fieldCount>0
    //   useRotation = enableRandomRotation || enableRandomAngularVelocity
    //   useOverride = fieldCount>0
    const bool needTrail = (pSettingsData_->enableTrail != 0) || fieldsActive;
    const bool needRotation = (pSettingsData_->enableRandomRotation != 0 || pSettingsData_->enableRandomAngularVelocity != 0);
    const bool needOverride = fieldsActive;

    if (needTrail && soaTrail_.allocatedCount < maxCount)
        AllocateSoABuffer(soaTrail_, maxCount);
    if (needRotation && soaRotation_.allocatedCount < maxCount)
        AllocateSoABuffer(soaRotation_, maxCount);
    if (needOverride && soaOverride_.allocatedCount < maxCount)
        AllocateSoABuffer(soaOverride_, maxCount);
    // 描画順->slot index は回転グループだけが読む（VS の enableRotation と同じ条件）。
    if (needRotation && soaRenderSlot_.allocatedCount < maxCount)
        AllocateSoABuffer(soaRenderSlot_, maxCount);
}

void ParticleCSGroup::CreatePerViewResource()
{
    perViewResource_ = pDxCommon_->CreateBufferResource(sizeof(PerView));
    perViewResource_->Map(0, nullptr, reinterpret_cast<void **>(&pPerViewData_));
    pPerViewData_->viewProjection = MakeIdentity4x4();
    pPerViewData_->billboardMatrix = MakeIdentity4x4();
}

void ParticleCSGroup::CreateMaterialResource()
{
    materialResource_ = pDxCommon_->CreateBufferResource(sizeof(ParticleMaterial));
    materialResource_->Map(0, nullptr, reinterpret_cast<void **>(&pMaterialData_));
    pMaterialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    pMaterialData_->uvTransform = MakeIdentity4x4();
}

void ParticleCSGroup::CreateIndexResource()
{
    // 複数メッシュ対応: 全メッシュのインデックスを連結し、頂点オフセットを考慮
    std::vector<uint32_t> allIndices;
    uint32_t vertexOffset = 0;
    for (const auto &mesh : modelData_.meshes)
    {
        for (auto idx : mesh.indices)
        {
            allIndices.push_back(idx + vertexOffset);
        }
        vertexOffset += static_cast<uint32_t>(mesh.vertices.size());
    }
    indexResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * allIndices.size());
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * allIndices.size());
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    indexResource_->Map(0, nullptr, reinterpret_cast<void **>(&indexData_));
    std::memcpy(indexData_, allIndices.data(), sizeof(uint32_t) * allIndices.size());
}

void ParticleCSGroup::CreateVertexResource()
{
    // クアッド用の頂点データ
    std::vector<VertexData> allVertices;
    for (const auto &mesh : modelData_.meshes)
    {
        allVertices.insert(allVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    }
    // 視錐台カリング用: モデルのローカル境界半径（原点からの最遠頂点）。
    // 粒子の世界半径 = max(scale) × この値 になるので、板ポリでも球でも同じ式で包める。
    float maxLenSq = 0.0f;
    for (const auto &v : allVertices)
    {
        const float lenSq = v.position.x * v.position.x + v.position.y * v.position.y + v.position.z * v.position.z;
        maxLenSq = (std::max)(maxLenSq, lenSq);
    }
    modelLocalRadius_ = (maxLenSq > 0.0f) ? std::sqrt(maxLenSq) : 1.0f;

    vertexResource_ = pDxCommon_->CreateBufferResource(sizeof(VertexData) * allVertices.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * allVertices.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
    vertexResource_->Map(0, nullptr, reinterpret_cast<void **>(&pVertexData_));
    std::memcpy(pVertexData_, allVertices.data(), sizeof(VertexData) * allVertices.size());
}

void ParticleCSGroup::CreatePerFrameResource()
{
    perFrameResource_ = pDxCommon_->CreateBufferResource(sizeof(PerFrame));
    perFrameResource_->Map(0, nullptr, reinterpret_cast<void **>(&pPerFrameData_));
    pPerFrameData_->time = 0.0f;
    pPerFrameData_->deltaTime = 0.0f;
    pPerFrameData_->groupId = 0;
}

void ParticleCSGroup::CreateFreeListIndexResource()
{
    freeListIndexResource_ = pDxCommon_->CreateBufferResource(sizeof(int), true);

    // UAV用のインデックス（Compute Shader用）
    freeListIndexSrvIndex_ = pSrvManager_->Allocate() + 1;
    freeListIndexSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(freeListIndexSrvIndex_);
    freeListIndexSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(freeListIndexSrvIndex_);
    pSrvManager_->CreateUAVStructuredBuffer(freeListIndexSrvIndex_, freeListIndexResource_.Get(), 1, sizeof(int));

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

    // リソース設定: int 1個分 (4バイト)
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(int32_t));

    // バッファ作成
    pDxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&freeListIndexReadbackBuffer_));
    freeListIndexReadbackBuffer_->SetName(L"FreeListIndex_Readback");
}

void ParticleCSGroup::CreateFreeListTrailIndexResource()
{
    freeListTrailIndexResource_ = pDxCommon_->CreateBufferResource(sizeof(int), true);

    freeListTrailIndexSrvIndex_ = pSrvManager_->Allocate() + 1;
    freeListTrailIndexSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(freeListTrailIndexSrvIndex_);
    freeListTrailIndexSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(freeListTrailIndexSrvIndex_);
    pSrvManager_->CreateUAVStructuredBuffer(freeListTrailIndexSrvIndex_, freeListTrailIndexResource_.Get(), 1, sizeof(int));

    // ★ Readbackバッファも作成
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(int32_t));

    pDxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&freeListTrailIndexReadbackBuffer_));
    freeListTrailIndexReadbackBuffer_->SetName(L"FreeListTrailIndex_Readback");
}

void ParticleCSGroup::CopyDebugDataToReadback()
{
    // === Head (freeListIndex) のコピー ===
    auto barrierHeadToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListIndexResource_.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    pCommandList_->ResourceBarrier(1, &barrierHeadToCopy);

    pCommandList_->CopyBufferRegion(
        freeListIndexReadbackBuffer_.Get(), 0,
        freeListIndexResource_.Get(), 0,
        sizeof(int32_t));

    auto barrierHeadToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListIndexResource_.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    pCommandList_->ResourceBarrier(1, &barrierHeadToUAV);

    // === Tail (freeListTrailIndex) のコピー ===
    auto barrierTailToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListTrailIndexResource_.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    pCommandList_->ResourceBarrier(1, &barrierTailToCopy);

    pCommandList_->CopyBufferRegion(
        freeListTrailIndexReadbackBuffer_.Get(), 0,
        freeListTrailIndexResource_.Get(), 0,
        sizeof(int32_t));

    auto barrierTailToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListTrailIndexResource_.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    pCommandList_->ResourceBarrier(1, &barrierTailToUAV);
}

void ParticleCSGroup::CreateFreeListResource()
{
    freeListResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * pSettingsData_->maxParticleCount, true);

    // UAV用のインデックス（Compute Shader用）
    freeListSrvIndex_ = pSrvManager_->Allocate() + 1;
    freeListSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(freeListSrvIndex_);
    freeListSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(freeListSrvIndex_);
    pSrvManager_->CreateUAVStructuredBuffer(freeListSrvIndex_, freeListResource_.Get(), pSettingsData_->maxParticleCount, sizeof(uint32_t));
}

void ParticleCSGroup::CreateSettingsResource()
{
    settingsResource_ = pDxCommon_->CreateBufferResource(sizeof(ParticleCSSettings));
    settingsResource_->Map(0, nullptr, reinterpret_cast<void **>(&pSettingsData_));

    // デフォルト設定
    pSettingsData_->lifeTimeMin = 1.0f;
    pSettingsData_->lifeTimeMax = 3.0f;
    pSettingsData_->scaleMin = 0.5f;
    pSettingsData_->scaleMax = 1.5f;
    pSettingsData_->velocityMin = {-0.25f, -0.25f, -0.25f};
    pSettingsData_->velocityMax = {0.25f, 0.25f, 0.25f};
    pSettingsData_->startColor = {1.0f, 1.0f, 1.0f, 1.0f};
    pSettingsData_->endColor = {1.0f, 1.0f, 1.0f, 0.0f};
    pSettingsData_->enableLifetimeScale = 0;
    pSettingsData_->enableRandomColor = 1;
    pSettingsData_->enableSinScale = 0;
    pSettingsData_->sinScaleFrequency = 5.0f;
    pSettingsData_->sinScaleAmplitude = 0.3f;
    pSettingsData_->maxParticleCount = 10000;
    pSettingsData_->emitCount = 0;
    pSettingsData_->enableGravity = 0;
    pSettingsData_->gravity = {0.0f, -9.8f, 0.0f};

    // トレイル設定のデフォルト値
    pSettingsData_->enableTrail = 0;
    pSettingsData_->trailSpawnDistance = 0.1f; // デフォルトを短く
    pSettingsData_->maxTrailPerParticle = 5;
    pSettingsData_->trailLifeTimeScale = 1.0f; // 親と同じ寿命割合に
    pSettingsData_->trailScaleMultiplier = {0.8f, 0.8f, 0.8f};
    pSettingsData_->trailColorMultiplier = {1.0f, 1.0f, 1.0f, 0.7f};
    pSettingsData_->trailVelocityScale = 0.3f;
    pSettingsData_->trailInheritVelocity = 1;
    pSettingsData_->trailMinLifeTime = 0.5f; // 最小寿命を長めに

    // ギャザー設定のデフォルト値を追加
    pSettingsData_->enableGather = 0;
    pSettingsData_->gatherStartRatio = 0.5f;
    pSettingsData_->gatherStrength = 2.0f;
    pSettingsData_->gatherTarget = {0.0f, 0.0f, 0.0f};

    pSettingsData_->enableAcceleration = 0;
    pSettingsData_->acceleration = {0.0f, 0.0f, 0.0f};
    pSettingsData_->enableVelocityDamping = 0;
    pSettingsData_->velocityDampingFactor = 0.98f;
    pSettingsData_->enableLifetimeVelocityDamping = 0;
    pSettingsData_->lifetimeVelocityDampingStart = 0.5f;
    pSettingsData_->enableRadialVelocity = 0;
    pSettingsData_->radialVelocityStrength = 1.0f;
    pSettingsData_->radialVelocityRandomness = 0.2f;
    pSettingsData_->radialVelocityCenter = {0.0f, 0.0f, 0.0f};

    pSettingsData_->enableCurlNoise = 0;
    pSettingsData_->curlNoiseScale = 1.0f;
    pSettingsData_->curlNoiseStrength = 1.2f;
    pSettingsData_->curlNoiseTimeScale = 0.2f;
    pSettingsData_->curlNoiseOctaves = 0;
    pSettingsData_->curlNoiseAttractStrength = 0.0f;
    pSettingsData_->curlNoiseBlendMode = 0;
    pSettingsData_->curlNoisePosRandomStrength = 0.0f;
    pSettingsData_->curlNoiseAttractCenter = {0.0f, 0.0f, 0.0f};

    // ---- 終了スケール デフォルト ----
    pSettingsData_->enableEndScale = 0;
    pSettingsData_->endScaleValue = {0.0f, 0.0f, 0.0f};

    // ---- 回転 デフォルト ----
    pSettingsData_->enableRandomRotation = 0;
    pSettingsData_->rotationMin = {0.0f, 0.0f, 0.0f};
    pSettingsData_->rotationMax = {0.0f, 0.0f, 0.0f};
    pSettingsData_->enableRandomAngularVelocity = 0;
    pSettingsData_->angularVelocityMin = {0.0f, 0.0f, 0.0f};
    pSettingsData_->angularVelocityMax = {0.0f, 0.0f, 0.0f};

    // ---- カラーグラデーション(N段) デフォルト ----
    pSettingsData_->enableColorGradient = 0;
    // 既定ストップ: 白(不透明) → 白(透明)。enableColorGradient を ON にすると LUT が使われる。
    colorStops_.clear();
    colorStops_.push_back(GradientStop{{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f});
    colorStops_.push_back(GradientStop{{1.0f, 1.0f, 1.0f, 0.0f}, 1.0f});
    colorStopsDirty_ = true;

    // ---- 寿命カーブ(サイズ/アルファ) デフォルト ----
    pSettingsData_->enableSizeCurve = 0;
    pSettingsData_->enableAlphaCurve = 0;
    // 既定カーブ: フラット(倍率1.0 = 変化なし)。ON にすると LUT が乗算される。
    sizeCurvePoints_ = {CurvePoint{0.0f, 1.0f}, CurvePoint{1.0f, 1.0f}};
    alphaCurvePoints_ = {CurvePoint{0.0f, 1.0f}, CurvePoint{1.0f, 1.0f}};
    lifeCurvesDirty_ = true;

    // ---- 音声振動 デフォルト ----
    pSettingsData_->enableAudioVibration = 0;
    pSettingsData_->audioVibrationStrength = 12.0f;
    pSettingsData_->audioVibrationSensitivity = 4.0f;
    pSettingsData_->audioAmplitude = 0.0f;
    pSettingsData_->audioVibrationFrequency = 22.0f;
    pSettingsData_->audioAttackSharpness = 1.8f;
    pSettingsData_->audioReleaseRate = 10.0f;
    pSettingsData_->audioPad0 = 0.0f;
    audioEnvelope_ = 0.0f;
    audioPrevPeak_ = 0.0f;
}

namespace {
// float RGBA[0,1] → RGBA8 パック（HLSL PackColorRGBA8 と一致: r | g<<8 | b<<16 | a<<24）。
uint32_t PackRGBA8(const Vector4 &c)
{
    auto q = [](float v) -> uint32_t {
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<uint32_t>(v * 255.0f + 0.5f);
    };
    return q(c.x) | (q(c.y) << 8) | (q(c.z) << 16) | (q(c.w) << 24);
}
// 寿命カーブ点列（x昇順）を t[0,1] で線形補間する。空なら 1.0（変化なし）。
float SampleCurve(const std::vector<CurvePoint> &pts, float t)
{
    if (pts.empty())
        return 1.0f;
    if (t <= pts.front().x)
        return pts.front().y;
    if (t >= pts.back().x)
        return pts.back().y;
    for (size_t i = 1; i < pts.size(); ++i)
    {
        if (t <= pts[i].x)
        {
            float span = pts[i].x - pts[i - 1].x;
            float u = span > 1e-6f ? (t - pts[i - 1].x) / span : 0.0f;
            return pts[i - 1].y + (pts[i].y - pts[i - 1].y) * u;
        }
    }
    return pts.back().y;
}
// 位置でソート済みのストップ列を lifeRatio t[0,1] で線形補間する。
Vector4 SampleGradient(const std::vector<GradientStop> &sorted, float t)
{
    if (sorted.empty())
        return {1.0f, 1.0f, 1.0f, 1.0f};
    if (t <= sorted.front().pos)
        return sorted.front().color;
    if (t >= sorted.back().pos)
        return sorted.back().color;
    for (size_t i = 1; i < sorted.size(); ++i)
    {
        if (t <= sorted[i].pos)
        {
            const Vector4 &a = sorted[i - 1].color;
            const Vector4 &b = sorted[i].color;
            float span = sorted[i].pos - sorted[i - 1].pos;
            float u = span > 1e-6f ? (t - sorted[i - 1].pos) / span : 0.0f;
            return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
                    a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u};
        }
    }
    return sorted.back().color;
}
} // namespace

void ParticleCSGroup::BakeColorLUT()
{
    // colorStops_ を位置でソートし、256段 RGBA8 LUT を pSettingsData_->colorLUT にベイクする。
    // ストップ数に依存しない O(256) で、GPU は LUT を1点サンプルするだけになる。
    std::vector<GradientStop> sorted = colorStops_;
    std::sort(sorted.begin(), sorted.end(),
              [](const GradientStop &a, const GradientStop &b) { return a.pos < b.pos; });
    for (int i = 0; i < 256; ++i)
    {
        float t = static_cast<float>(i) / 255.0f;
        pSettingsData_->colorLUT[i] = PackRGBA8(SampleGradient(sorted, t));
    }
}

void ParticleCSGroup::BakeLifetimeCurveLUTs()
{
    // サイズ/アルファの倍率カーブを 256段 float LUT にベイクする。点が無ければ全 1.0（変化なし）。
    std::vector<CurvePoint> sz = sizeCurvePoints_;
    std::vector<CurvePoint> al = alphaCurvePoints_;
    std::sort(sz.begin(), sz.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
    std::sort(al.begin(), al.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
    for (int i = 0; i < 256; ++i)
    {
        float t = static_cast<float>(i) / 255.0f;
        pSettingsData_->sizeCurveLUT[i] = SampleCurve(sz, t);
        pSettingsData_->alphaCurveLUT[i] = SampleCurve(al, t);
    }
}

void ParticleCSGroup::CreateAliveCountResource()
{
    // GPU側のカウント用バッファ (UAV)
    aliveCountResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t), true);

    aliveCountSrvIndex_ = pSrvManager_->Allocate() + 1;
    aliveCountSrvHandle_.first = pSrvManager_->GetCPUDescriptorHandle(aliveCountSrvIndex_);
    aliveCountSrvHandle_.second = pSrvManager_->GetGPUDescriptorHandle(aliveCountSrvIndex_);
    pSrvManager_->CreateUAVStructuredBuffer(aliveCountSrvIndex_, aliveCountResource_.Get(), 1, sizeof(uint32_t));

    // CPU読み取り用のReadbackバッファ
    D3D12_HEAP_PROPERTIES readbackHeapProps{};
    readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = sizeof(uint32_t);
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    pDxCommon_->GetDevice()->CreateCommittedResource(
        &readbackHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&aliveCountReadbackResource_));
}

void ParticleCSGroup::CreateAliveListResources()
{
    const uint32_t maxCount = pSettingsData_->maxParticleCount;

    // ping-pong の2枚それぞれに aliveList / aliveCounter を本確保する。
    for (uint32_t i = 0; i < kAlivePingPong; ++i)
    {
        // --- aliveList: 生存 slot index バッファ (UAV: compute u9 / SRV: VS t2) ---
        aliveListResource_[i] = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * maxCount, true);

        aliveListUavIndex_[i] = pSrvManager_->Allocate() + 1;
        aliveListUavHandle_[i].first = pSrvManager_->GetCPUDescriptorHandle(aliveListUavIndex_[i]);
        aliveListUavHandle_[i].second = pSrvManager_->GetGPUDescriptorHandle(aliveListUavIndex_[i]);
        pSrvManager_->CreateUAVStructuredBuffer(aliveListUavIndex_[i], aliveListResource_[i].Get(), maxCount, sizeof(uint32_t));

        aliveListSrvForVSIndex_[i] = pSrvManager_->Allocate() + 1;
        pSrvManager_->CreateSRVforStructuredBuffer(aliveListSrvForVSIndex_[i], aliveListResource_[i].Get(), maxCount, sizeof(uint32_t));

        // --- aliveCounter: 生存数アトミックカウンタ (UAV: compute u10 / SRV: VS t3) ---
        aliveCounterResource_[i] = pDxCommon_->CreateBufferResource(sizeof(uint32_t), true);

        aliveCounterUavIndex_[i] = pSrvManager_->Allocate() + 1;
        aliveCounterUavHandle_[i].first = pSrvManager_->GetCPUDescriptorHandle(aliveCounterUavIndex_[i]);
        aliveCounterUavHandle_[i].second = pSrvManager_->GetGPUDescriptorHandle(aliveCounterUavIndex_[i]);
        pSrvManager_->CreateUAVStructuredBuffer(aliveCounterUavIndex_[i], aliveCounterResource_[i].Get(), 1, sizeof(uint32_t));

        aliveCounterSrvForVSIndex_[i] = pSrvManager_->Allocate() + 1;
        pSrvManager_->CreateSRVforStructuredBuffer(aliveCounterSrvForVSIndex_[i], aliveCounterResource_[i].Get(), 1, sizeof(uint32_t));
    }

    // --- visibleCounter: 描画リスト長（視錐台カリング後）のアトミックカウンタ ---
    // UAV: compute u12 / SRV: VS t3 / ExecuteIndirect の instanceCount のコピー元。
    // 生存リストと違い当フレーム限りの値なので ping-pong しない。
    visibleCounterResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t), true);
    visibleCounterResource_->SetName(L"VisibleCounter");

    visibleCounterUavIndex_ = pSrvManager_->Allocate() + 1;
    visibleCounterUavHandle_.first = pSrvManager_->GetCPUDescriptorHandle(visibleCounterUavIndex_);
    visibleCounterUavHandle_.second = pSrvManager_->GetGPUDescriptorHandle(visibleCounterUavIndex_);
    pSrvManager_->CreateUAVStructuredBuffer(visibleCounterUavIndex_, visibleCounterResource_.Get(), 1, sizeof(uint32_t));

    visibleCounterSrvForVSIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateSRVforStructuredBuffer(visibleCounterSrvForVSIndex_, visibleCounterResource_.Get(), 1, sizeof(uint32_t));

    // CPU 読み取り用 Readback バッファ（out からコピーする共有 1個）
    D3D12_HEAP_PROPERTIES readbackHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t));
    pDxCommon_->GetDevice()->CreateCommittedResource(
        &readbackHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&aliveCounterReadbackResource_));
    aliveCounterReadbackResource_->SetName(L"AliveCounter_Readback");
}

void ParticleCSGroup::CreateDrawArgsResources()
{
    // GPU駆動描画(DrawInstanceIndirect)の引数バッファ。
    //   固定部(IndexCountPerInstance / Start*) は CPU が一度だけ書き、
    //   InstanceCount(オフセット+4) だけを毎フレーム GPU 上のカウンタからコピーする。
    //   → 描画数の決定に CPU 読み戻しが要らなくなる（1〜2F遅延＋マージン発行の解消）。
    drawArgsCount_ = static_cast<uint32_t>(modelData_.meshes.size());
    if (drawArgsCount_ == 0)
    {
        drawArgsCount_ = 1;
    }
    const size_t argsBytes = static_cast<size_t>(kDrawArgsStride) * drawArgsCount_;

    // 引数バッファ本体（DEFAULT ヒープ）。状態は COMMON のまま扱う（暗黙昇格/減衰に任せる）。
    D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC argsDesc = CD3DX12_RESOURCE_DESC::Buffer(argsBytes);
    pDxCommon_->GetDevice()->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &argsDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&drawArgsResource_));
    drawArgsResource_->SetName(L"ParticleDrawArgs_Indirect");

    // 固定部の種（UPLOAD ヒープ）。作成時に書いたら以降 CPU は触らない（GPU との競合なし）。
    drawArgsUploadResource_ = pDxCommon_->CreateBufferResource(argsBytes);
    D3D12_DRAW_INDEXED_ARGUMENTS *pArgs = nullptr;
    drawArgsUploadResource_->Map(0, nullptr, reinterpret_cast<void **>(&pArgs));
    for (uint32_t i = 0; i < drawArgsCount_; ++i)
    {
        const uint32_t indexCount = (i < modelData_.meshes.size())
                                        ? static_cast<uint32_t>(modelData_.meshes[i].indices.size())
                                        : 0u;
        pArgs[i].IndexCountPerInstance = indexCount;
        pArgs[i].InstanceCount = 0; // 実値は毎フレーム GPU カウンタからコピーされる
        pArgs[i].StartIndexLocation = 0;
        pArgs[i].BaseVertexLocation = 0;
        pArgs[i].StartInstanceLocation = 0;
    }
    drawArgsUploadResource_->Unmap(0, nullptr);

    drawArgsStaticUploaded_ = false;
    drawArgsReady_ = false;
}

void ParticleCSGroup::CountAliveParticles()
{
    // CountParticle.CSを実行
    particleCommon_->ComputeCountDrawCommonSetting();

    pCommandList_->SetComputeRootConstantBufferView(0, settingsResource_->GetGPUVirtualAddress());
    pCommandList_->SetComputeRootDescriptorTable(1, aliveCountSrvHandle_.second);
    // SoA: 生存判定は Life バッファ(u1)で行う
    pCommandList_->SetComputeRootDescriptorTable(2, soaLife_.uavHandle.second);

    int dispatchCount = (pSettingsData_->maxParticleCount + threadsPerGroup_ - 1) / threadsPerGroup_;
    pCommandList_->Dispatch(dispatchCount, 1, 1);

    // UAVバリア（UAV書き込み完了を保証）
    pDxCommon_->TransitionUAVBarrier(aliveCountResource_.Get());

    // CopyResource前にリソース状態を遷移（自作関数を使う）
    pDxCommon_->BarrierTransition(
        aliveCountResource_.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    // GPU→CPUへコピー
    pCommandList_->CopyResource(aliveCountReadbackResource_.Get(), aliveCountResource_.Get());

    // 戻す（次のDispatch用に再びUAV状態へ）
    pDxCommon_->BarrierTransition(
        aliveCountResource_.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

uint32_t ParticleCSGroup::GetAliveParticleCount()
{
    // 旧 CountParticle 全Nディスパッチを廃止し、生存コンパクションの
    // aliveCounter 読み戻し値(FetchAliveDrawCount で更新)をそのまま統計に流用する。
    cachedAliveCount_ = aliveDrawCount_;
    return cachedAliveCount_;
}
} // namespace Hagine
