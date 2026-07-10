#define NOMINMAX
#include "ParticleCSGroup.h"
#include <Asset/AssetPath.h>
#include <Audio/Audio.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <Frame.h>
#include <Graphics/Model/ModelManager.h>
#include <Graphics/PipeLine/ComputePipeLineManager.h>
#include <Line/DrawLine3D.h>
#include <d3dx12.h>
#ifdef _DEBUG
#include <implot.h>
#endif // DEBUG
#ifdef USE_IMGUI
// ※ namespace Hagine の外で include すること。ImGradient.h は `struct ImVec4;` を前方宣言するため、
//   namespace 内で include すると Hagine::ImVec4(不完全型) が生成され全 ImVec4 参照が壊れる。
#include "imgui.h"
#include "ImGradient.h"
#include "ImCurveEdit.h"
#include "Utility/Debug/ImGui/AssetDragDrop.h"
#endif

namespace Hagine {
void ParticleCSGroup::Initialize(uint32_t maxParticleCount) {
    dxCommon_ = ParticleCommon::GetInstance()->GetDxCommon();
    srvManager_ = SrvManager::GetInstance();
    particleCommon_ = ParticleCommon::GetInstance();
    texManager_ = TextureManager::GetInstance();
    commandList_ = dxCommon_->GetCommandList().Get();
    computeCommandList_ = dxCommon_->GetComputeCommandList().Get();
    CreateSettingsResource();
    settingsData_->maxParticleCount = maxParticleCount;
    CreateParticleSoABuffers();
    CreatePerViewResource();
    CreatePerFrameResource();
    CreateFreeListIndexResource();
    CreateFreeListTrailIndexResource();
    CreateFreeListResource();
    CreateAliveCountResource();
    CreateAliveListResources();

    perViewData_->enableBillboard = 1;

    isInitialized_ = true;
}

int ParticleCSGroup::CalculateOptimalEmitCount() const {
    if (frequency_ <= 0.0f || settingsData_->lifeTimeMax <= 0.0f) {
        return static_cast<int>(settingsData_->maxParticleCount);
    }

    float emissionCount = settingsData_->lifeTimeMax / frequency_;

    int result;
    if (emissionCount <= 1.0f) {
        result = static_cast<int>(settingsData_->maxParticleCount);
    } else {
        result = static_cast<int>(settingsData_->maxParticleCount / emissionCount);
    }

    return std::clamp(result, 1, static_cast<int>(settingsData_->maxParticleCount));
}

ParticleCSGroup::~ParticleCSGroup() {
    if (!isInitialized_) {
        return;
    }

    // Map済みリソースのUnmap
    if (settingsResource_) {
        settingsResource_->Unmap(0, nullptr);
    }
    if (perViewResource_) {
        perViewResource_->Unmap(0, nullptr);
    }
    if (perFrameResource_) {
        perFrameResource_->Unmap(0, nullptr);
    }
    if (materialResource_) {
        materialResource_->Unmap(0, nullptr);
    }
    if (vertexResource_) {
        vertexResource_->Unmap(0, nullptr);
    }
    if (indexResource_) {
        indexResource_->Unmap(0, nullptr);
    }
}

ParticleCSGroupData ParticleCSGroup::CreateParticleGroup(const std::string &groupName, const std::string &filename, uint32_t maxParticleCount, const std::string &texturePath, BlendMode blendMode) {
    Initialize(maxParticleCount);
    particleGroupData_.groupName = groupName;
    modelFilePath_ = filename;
    ModelManager::GetInstance()->LoadModel(filename);
    model_ = ModelManager::GetInstance()->FindModel(filename);
    modelData_ = model_->GetModelData();
    CreateVertexResource();
    CreateIndexResource();
    // マテリアルが複数ある場合は最初のものを使う
    particleGroupData_.materials.clear();
    if (texturePath.empty()) {
        if (!modelData_.materials.empty()) {
            particleGroupData_.materials = ForParticleMaterials(modelData_.materials);
        } else {
            particleGroupData_.materials.push_back(ParticleMaterial{});
        }
    } else {
        ParticleMaterial mat;
        mat.textureFilePath = texturePath;
        mat.textureIndex = texManager_->GetTextureIndexByFilePath(texturePath);
        particleGroupData_.materials.push_back(mat);
    }
    // すべてのマテリアルのテクスチャをロード
    for (auto &mat : particleGroupData_.materials) {
        texManager_->LoadTexture(mat.textureFilePath);
        mat.textureIndex = texManager_->GetTextureIndexByFilePath(mat.textureFilePath);
    }

    CreateMaterialResource();

    InitParticle();
    particleGroupData_.blendMode = blendMode;
    return particleGroupData_;
}

ParticleCSGroupData ParticleCSGroup::CreatePrimitiveParticleGroup(const std::string &groupName, PrimitiveType type, uint32_t maxParticleCount, const std::string &texturePath, BlendMode blendMode) {
    Initialize(maxParticleCount);
    particleGroupData_.groupName = groupName;
    type_ = type;
    model_ = ModelManager::GetInstance()->FindModel(ModelManager::GetInstance()->CreatePrimitiveModel(type, texturePath));
    texManager_->LoadTexture(texturePath);
    modelData_ = model_->GetModelData();
    CreateVertexResource();
    CreateIndexResource();
    // マテリアルが複数ある場合は最初のものを使う
    particleGroupData_.materials.clear();
    if (texturePath.empty()) {
        if (!modelData_.materials.empty()) {
            particleGroupData_.materials = ForParticleMaterials(modelData_.materials);
        } else {
            particleGroupData_.materials.push_back(ParticleMaterial{});
        }
    } else {
        ParticleMaterial mat;
        mat.textureFilePath = texturePath;
        mat.textureIndex = texManager_->GetTextureIndexByFilePath(texturePath);
        particleGroupData_.materials.push_back(mat);
    }
    // すべてのマテリアルのテクスチャをロード
    for (auto &mat : particleGroupData_.materials) {
        texManager_->LoadTexture(mat.textureFilePath);
        mat.textureIndex = texManager_->GetTextureIndexByFilePath(mat.textureFilePath);
    }

    CreateMaterialResource();

    InitParticle();
    particleGroupData_.blendMode = blendMode;

    return particleGroupData_;
}

void ParticleCSGroup::SetTexture(const std::string &path) {
    if (path.empty() || particleGroupData_.materials.empty())
        return;
    texManager_->LoadTexture(path);
    uint32_t index = texManager_->GetTextureIndexByFilePath(path);
    // 描画は毎フレーム textureFilePath で SRV を引くのでパス差し替えで即時反映される。
    // textureIndex も一応更新しておく。
    for (auto &m : particleGroupData_.materials) {
        m.textureFilePath = path;
        m.textureIndex = index;
    }
}

void ParticleCSGroup::InitParticle() {
    srvManager_->SetDescriptorHeap();

    dxCommon_->TransitionUAVBarrier(soaLife_.resource.Get());

    // InitParticle.CS: SoA は Life バッファ(u0)のみ初期化すればよい
    particleCommon_->ComputeInitDrawCommonSetting();
    commandList_->SetComputeRootDescriptorTable(0, soaLife_.uavHandle.second);
    commandList_->SetComputeRootDescriptorTable(1, freeListIndexSrvHandle_.second);
    commandList_->SetComputeRootDescriptorTable(2, freeListSrvHandle_.second);
    commandList_->SetComputeRootDescriptorTable(3, freeListTrailIndexSrvHandle_.second);
    commandList_->SetComputeRootConstantBufferView(4, settingsResource_->GetGPUVirtualAddress());
    int disPatchCount = (settingsData_->maxParticleCount + threadsPerGroup_ - 1) / threadsPerGroup_;
    commandList_->Dispatch(disPatchCount, 1, 1);

    dxCommon_->TransitionSRVBarrier();
}

bool ParticleCSGroup::CanUseLiteUpdate(bool fieldsActive) const {
    // フィールドの影響を受けるグループはフル版必須（force-trail/override/colorMul 等）。
    if (fieldsActive)
        return false;
    const ParticleCSSettings *s = settingsData_;
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
    ID3D12GraphicsCommandList *cmdList) {
    // フル版が Trail/Rotation/Override を触る場合のみ、ここで本確保へ作り直す
    // （演出なしグループは 1要素ダミーのままで VRAM を節約）。バインドより前に行う。
    EnsureUpdateOptionalBuffers(fieldsActive);

    // cmdList が渡された場合はそちら（非同期 Compute Queue）を使う
    ID3D12GraphicsCommandList *cl = cmdList ? cmdList : commandList_;
    auto *computePSOMgr = ComputePipeLineManager::GetInstance();
    // 演出なしグループは軽量 PSO を使う。root sig はフル版と共有なので
    // バインド（下記 17 パラメータ）は両者で同一。軽量シェーダは未使用の
    // テーブルを無視するだけで安全。
    const ComputePipelineType updateType = CanUseLiteUpdate(fieldsActive)
                                               ? ComputePipelineType::kUpdateEmitterLite
                                               : ComputePipelineType::kUpdateEmitter;
    computePSOMgr->DrawCommonSetting(updateType,
                                     BlendMode::kNormal, ShaderMode::kNone, cl);
    // SoA UAV (u0-u5)
    cl->SetComputeRootDescriptorTable(0, soaLife_.uavHandle.second);
    cl->SetComputeRootDescriptorTable(1, soaDrawCore_.uavHandle.second);
    cl->SetComputeRootDescriptorTable(2, soaSimCore_.uavHandle.second);
    cl->SetComputeRootDescriptorTable(3, soaTrail_.uavHandle.second);
    cl->SetComputeRootDescriptorTable(4, soaRotation_.uavHandle.second);
    cl->SetComputeRootDescriptorTable(5, soaOverride_.uavHandle.second);
    // フリーリスト (u6-u8)
    cl->SetComputeRootDescriptorTable(6, freeListIndexSrvHandle_.second);
    cl->SetComputeRootDescriptorTable(7, freeListSrvHandle_.second);
    cl->SetComputeRootDescriptorTable(8, freeListTrailIndexSrvHandle_.second);
    // 生存コンパクション (u9-u10): out フェーズへ書き出す
    cl->SetComputeRootDescriptorTable(9, aliveListUavHandle_[alivePhase_].second);
    cl->SetComputeRootDescriptorTable(10, aliveCounterUavHandle_[alivePhase_].second);
    // 描画コンパクション (u11)
    cl->SetComputeRootDescriptorTable(11, soaRenderCompact_.uavHandle.second);
    // CBV (b0-b2) / SRV (t0-t1)
    cl->SetComputeRootConstantBufferView(12, perFrameResource_->GetGPUVirtualAddress());
    cl->SetComputeRootConstantBufferView(13, settingsResource_->GetGPUVirtualAddress());
    cl->SetComputeRootConstantBufferView(14, fieldCountResource->GetGPUVirtualAddress());
    cl->SetComputeRootDescriptorTable(15, fieldsSrvHandle.second);
    cl->SetComputeRootDescriptorTable(16, overrideSrvHandle.second);
    // 生存リスト間接ディスパッチ (t2,t3): in リスト/カウンタ = 前フレームの out フェーズ
    const uint32_t inIdx = alivePhase_ ^ 1u;
    cl->SetComputeRootDescriptorTable(17, srvManager_->GetGPUDescriptorHandle(aliveListSrvForVSIndex_[inIdx]));
    cl->SetComputeRootDescriptorTable(18, srvManager_->GetGPUDescriptorHandle(aliveCounterSrvForVSIndex_[inIdx]));

    // 軽量版・フル版ともスレッドグループ256（Ampere の常駐1536上限で占有率を上げる狙い）。
    // 各シェーダの [numthreads] と一致必須（Lite=UpdateParticleLite / Full=UpdateParticle）。
    const uint32_t groupSize = (updateType == ComputePipelineType::kUpdateEmitterLite)
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
    const uint32_t maxCount = settingsData_->maxParticleCount;
    uint32_t inLenEst = aliveDrawCount_;
    if (inLenEst > maxCount)
        inLenEst = maxCount; // 初回フレーム等の未初期化/異常値ガード（オーバーフロー防止）
    uint32_t threadCount = inLenEst + inLenEst / 4u + settingsData_->emitCount + 4096u;
    if (threadCount > maxCount)
        threadCount = maxCount;
    int disPatchCount = (threadCount + groupSize - 1) / groupSize;
    if (disPatchCount < 1)
        disPatchCount = 1;
    cl->Dispatch(disPatchCount, 1, 1);
}

void ParticleCSGroup::ResetAliveCounterDispatch(ID3D12GraphicsCommandList *cmdList) {
    ID3D12GraphicsCommandList *cl = cmdList ? cmdList : commandList_;
    ComputePipeLineManager::GetInstance()->DrawCommonSetting(
        ComputePipelineType::kResetArgs, BlendMode::kNormal, ShaderMode::kNone, cl);
    // out フェーズのカウンタを 0 にリセットする。
    cl->SetComputeRootDescriptorTable(0, aliveCounterUavHandle_[alivePhase_].second);
    cl->Dispatch(1, 1, 1);

    // リセット完了を Update の InterlockedAdd より前に保証する
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = aliveCounterResource_[alivePhase_].Get();
    cl->ResourceBarrier(1, &uavBarrier);
}

void ParticleCSGroup::RecordAliveCountReadback(ID3D12GraphicsCommandList *computeCmdList) {
    // Update が compute queue で書いた直後（バッファが UAV 状態）にコピーする。
    // この時点でカウンタは UnorderedAccess へ昇格済みなので状態遷移は整合する。
    ID3D12GraphicsCommandList *cl = computeCmdList ? computeCmdList : commandList_;

    // out フェーズのカウンタを読み戻す（共有 readback へコピー）。
    ID3D12Resource *counterRes = aliveCounterResource_[alivePhase_].Get();

    // Update の append 完了を保証
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = counterRes;
    cl->ResourceBarrier(1, &uavBarrier);

    auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        counterRes,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    cl->ResourceBarrier(1, &toCopy);

    cl->CopyResource(aliveCounterReadbackResource_.Get(), counterRes);

    auto toUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        counterRes,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cl->ResourceBarrier(1, &toUAV);
}

void ParticleCSGroup::FetchAliveDrawCount() {
    // 直近フレームにコピー済みの値を読み取る（1〜2フレーム遅延・許容）
    uint32_t *mappedData = nullptr;
    D3D12_RANGE readRange{0, sizeof(uint32_t)};
    HRESULT hr = aliveCounterReadbackResource_->Map(0, &readRange, reinterpret_cast<void **>(&mappedData));
    if (SUCCEEDED(hr) && mappedData) {
        aliveDrawCount_ = *mappedData;
        aliveCounterReadbackResource_->Unmap(0, nullptr);
    }
}

void ParticleCSGroup::Update(const ViewProjection &vp) {
    perFrameData_->time += Frame::DeltaTime();
    perFrameData_->deltaTime = Frame::DeltaTime();

    // カラーグラデーション: ストップが変更された(dirty)なら 256段 LUT を CB へ再ベイクする。
    // 有効時のみベイク（OFF のグループは LUT を読まないので無駄を省く）。
    if (settingsData_->enableColorGradient != 0 && colorStopsDirty_) {
        BakeColorLUT();
        colorStopsDirty_ = false;
    }
    // 寿命カーブ(サイズ/アルファ)も同様に dirty 時のみ再ベイク（どちらか有効なら）。
    if ((settingsData_->enableSizeCurve != 0 || settingsData_->enableAlphaCurve != 0) && lifeCurvesDirty_) {
        BakeLifetimeCurveLUTs();
        lifeCurvesDirty_ = false;
    }

    // 音声振動: 有効なときだけ「音の立ち上がり(onset)」からエンベロープを作って CB に注入する。
    //   onset  = 今のピーク − 前フレームのピーク の正の部分（＝音が大きくなった“増加分”）。
    //   エンベロープ = 時間で指数減衰させつつ onset で即座に跳ね上げる（アタック即・リリース減衰）。
    //   → 波形が大きくなった瞬間にバンっと跳ね、その後スッと落ち着く（GPU が振動の駆動に使う）。
    // OFF のグループは触らない＝無回帰。Audio 参照は有効時のみで軽量（再生中ボイスの PCM をサンプルするだけ）。
    if (settingsData_->enableAudioVibration != 0) {
        const float peak = Audio::GetInstance()->GetCurrentAmplitude(); // [0,1] 現在のピーク
        const float dt = Frame::DeltaTime();
        const float onset = (std::max)(0.0f, peak - audioPrevPeak_);    // 立ち上がり（増加分）
        audioPrevPeak_ = peak;
        // リリース: releaseRate[1/s] が大きいほど早く落ち着く（フレームレート非依存な指数減衰）
        const float releaseRate = (settingsData_->audioReleaseRate > 0.0f) ? settingsData_->audioReleaseRate : 10.0f;
        audioEnvelope_ *= std::exp(-releaseRate * dt);
        // アタック: onset の方が大きければ即座に跳ね上げる（＝バンっ）
        audioEnvelope_ = (std::max)(audioEnvelope_, onset);
        settingsData_->audioAmplitude = audioEnvelope_;
    } else {
        audioEnvelope_ = 0.0f;
        audioPrevPeak_ = 0.0f;
        settingsData_->audioAmplitude = 0.0f;
    }

    perViewData_->viewProjection = vp.matView_ * vp.matProjection_;
    // 距離カリング(overdraw 対策)用のカメラワールド座標。enableDistanceCull 等の設定値は
    // ImGui/ロードで設定された perView の値をそのまま保持する（Update では上書きしない）。
    perViewData_->cameraPosition = vp.translation_;
    // 画面サイズ上限/微小カリング用の射影スケール（projection[1][1] = cot(fovY/2)）。
    perViewData_->projScaleY = vp.matProjection_.m[1][1];
    // 回転を使わないグループは VS の回転行列計算（sincos×3＋行列積）を省くためのフラグ。
    perViewData_->enableRotation =
        (settingsData_->enableRandomRotation != 0 || settingsData_->enableRandomAngularVelocity != 0) ? 1u : 0u;
    if (perViewData_->enableBillboard) {
        perViewData_->billboardMatrix = vp.matView_;
        perViewData_->billboardMatrix.m[3][0] = 0.0f;
        perViewData_->billboardMatrix.m[3][1] = 0.0f;
        perViewData_->billboardMatrix.m[3][2] = 0.0f;
        perViewData_->billboardMatrix.m[3][3] = 1.0f;
        perViewData_->billboardMatrix = Inverse(perViewData_->billboardMatrix);
    } else {
        perViewData_->billboardMatrix = MakeIdentity4x4();
    }

    CopyDebugDataToReadback();
}

void ParticleCSGroup::AllocateSoABuffer(SoABuffer &buf, uint32_t count) {
    if (count == 0)
        count = 1;
    // 旧リソースは in-flight のコマンドリストが参照中の可能性があるため即解放しない。
    // 退避先へ移し、グループ破棄まで生かす（ダミーは要素1個なので極小）。
    if (buf.resource) {
        retiredSoABuffers_.push_back(buf.resource);
    }
    buf.resource = dxCommon_->CreateBufferResource(static_cast<size_t>(buf.stride) * count, true);
    // 既存ディスクリプタ枠を上書きすると in-flight 参照とハザードになるため、
    // 毎回「新しい枠」を確保して作り直す（SrvManager は bump 割当なので枠は使い捨て）。
    buf.uavIndex = srvManager_->Allocate() + 1;
    buf.uavHandle.first = srvManager_->GetCPUDescriptorHandle(buf.uavIndex);
    buf.uavHandle.second = srvManager_->GetGPUDescriptorHandle(buf.uavIndex);
    srvManager_->CreateUAVStructuredBuffer(buf.uavIndex, buf.resource.Get(), count, buf.stride);
    if (buf.withSrvForVS) {
        buf.srvForVSIndex = srvManager_->Allocate() + 1;
        srvManager_->CreateSRVforStructuredBuffer(buf.srvForVSIndex, buf.resource.Get(), count, buf.stride);
    }
    buf.allocatedCount = count;
}

void ParticleCSGroup::CreateParticleSoABuffers() {
    const uint32_t maxCount = settingsData_->maxParticleCount;

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
}

void ParticleCSGroup::EnsureUpdateOptionalBuffers(bool fieldsActive) {
    const uint32_t maxCount = settingsData_->maxParticleCount;
    // フル版 Update のバッファ load/store ゲートと一致させる:
    //   useTrail    = enableTrail || fieldCount>0
    //   useRotation = enableRandomRotation || enableRandomAngularVelocity
    //   useOverride = fieldCount>0
    const bool needTrail = (settingsData_->enableTrail != 0) || fieldsActive;
    const bool needRotation = (settingsData_->enableRandomRotation != 0 || settingsData_->enableRandomAngularVelocity != 0);
    const bool needOverride = fieldsActive;

    if (needTrail && soaTrail_.allocatedCount < maxCount)
        AllocateSoABuffer(soaTrail_, maxCount);
    if (needRotation && soaRotation_.allocatedCount < maxCount)
        AllocateSoABuffer(soaRotation_, maxCount);
    if (needOverride && soaOverride_.allocatedCount < maxCount)
        AllocateSoABuffer(soaOverride_, maxCount);
}

void ParticleCSGroup::CreatePerViewResource() {
    perViewResource_ = dxCommon_->CreateBufferResource(sizeof(PerView));
    perViewResource_->Map(0, nullptr, reinterpret_cast<void **>(&perViewData_));
    perViewData_->viewProjection = MakeIdentity4x4();
    perViewData_->billboardMatrix = MakeIdentity4x4();
}

void ParticleCSGroup::CreateMaterialResource() {
    materialResource_ = dxCommon_->CreateBufferResource(sizeof(ParticleMaterial));
    materialResource_->Map(0, nullptr, reinterpret_cast<void **>(&materialData_));
    materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    materialData_->uvTransform = MakeIdentity4x4();
}

void ParticleCSGroup::CreateIndexResource() {
    // 複数メッシュ対応: 全メッシュのインデックスを連結し、頂点オフセットを考慮
    std::vector<uint32_t> allIndices;
    uint32_t vertexOffset = 0;
    for (const auto &mesh : modelData_.meshes) {
        for (auto idx : mesh.indices) {
            allIndices.push_back(idx + vertexOffset);
        }
        vertexOffset += static_cast<uint32_t>(mesh.vertices.size());
    }
    indexResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * allIndices.size());
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * allIndices.size());
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    indexResource_->Map(0, nullptr, reinterpret_cast<void **>(&indexData_));
    std::memcpy(indexData_, allIndices.data(), sizeof(uint32_t) * allIndices.size());
}

void ParticleCSGroup::CreateVertexResource() {
    // クアッド用の頂点データ
    std::vector<VertexData> allVertices;
    for (const auto &mesh : modelData_.meshes) {
        allVertices.insert(allVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    }
    vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * allVertices.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * allVertices.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
    vertexResource_->Map(0, nullptr, reinterpret_cast<void **>(&vertexData_));
    std::memcpy(vertexData_, allVertices.data(), sizeof(VertexData) * allVertices.size());
}

void ParticleCSGroup::CreatePerFrameResource() {
    perFrameResource_ = dxCommon_->CreateBufferResource(sizeof(PerFrame));
    perFrameResource_->Map(0, nullptr, reinterpret_cast<void **>(&perFrameData_));
    perFrameData_->time = 0.0f;
    perFrameData_->deltaTime = 0.0f;
    perFrameData_->groupId = 0;
}

void ParticleCSGroup::CreateFreeListIndexResource() {
    freeListIndexResource_ = dxCommon_->CreateBufferResource(sizeof(int), true);

    // UAV用のインデックス（Compute Shader用）
    freeListIndexSrvIndex_ = srvManager_->Allocate() + 1;
    freeListIndexSrvHandle_.first = srvManager_->GetCPUDescriptorHandle(freeListIndexSrvIndex_);
    freeListIndexSrvHandle_.second = srvManager_->GetGPUDescriptorHandle(freeListIndexSrvIndex_);
    srvManager_->CreateUAVStructuredBuffer(freeListIndexSrvIndex_, freeListIndexResource_.Get(), 1, sizeof(int));

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

    // リソース設定: int 1個分 (4バイト)
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(int32_t));

    // バッファ作成
    dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&freeListIndexReadbackBuffer_));
    freeListIndexReadbackBuffer_->SetName(L"FreeListIndex_Readback");
}

void ParticleCSGroup::CreateFreeListTrailIndexResource() {
    freeListTrailIndexResource_ = dxCommon_->CreateBufferResource(sizeof(int), true);

    freeListTrailIndexSrvIndex_ = srvManager_->Allocate() + 1;
    freeListTrailIndexSrvHandle_.first = srvManager_->GetCPUDescriptorHandle(freeListTrailIndexSrvIndex_);
    freeListTrailIndexSrvHandle_.second = srvManager_->GetGPUDescriptorHandle(freeListTrailIndexSrvIndex_);
    srvManager_->CreateUAVStructuredBuffer(freeListTrailIndexSrvIndex_, freeListTrailIndexResource_.Get(), 1, sizeof(int));

    // ★ Readbackバッファも作成
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(int32_t));

    dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&freeListTrailIndexReadbackBuffer_));
    freeListTrailIndexReadbackBuffer_->SetName(L"FreeListTrailIndex_Readback");
}

void ParticleCSGroup::CopyDebugDataToReadback() {
    // === Head (freeListIndex) のコピー ===
    auto barrierHeadToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListIndexResource_.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList_->ResourceBarrier(1, &barrierHeadToCopy);

    commandList_->CopyBufferRegion(
        freeListIndexReadbackBuffer_.Get(), 0,
        freeListIndexResource_.Get(), 0,
        sizeof(int32_t));

    auto barrierHeadToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListIndexResource_.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList_->ResourceBarrier(1, &barrierHeadToUAV);

    // === Tail (freeListTrailIndex) のコピー ===
    auto barrierTailToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListTrailIndexResource_.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList_->ResourceBarrier(1, &barrierTailToCopy);

    commandList_->CopyBufferRegion(
        freeListTrailIndexReadbackBuffer_.Get(), 0,
        freeListTrailIndexResource_.Get(), 0,
        sizeof(int32_t));

    auto barrierTailToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListTrailIndexResource_.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList_->ResourceBarrier(1, &barrierTailToUAV);
}

void ParticleCSGroup::CreateFreeListResource() {
    freeListResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * settingsData_->maxParticleCount, true);

    // UAV用のインデックス（Compute Shader用）
    freeListSrvIndex_ = srvManager_->Allocate() + 1;
    freeListSrvHandle_.first = srvManager_->GetCPUDescriptorHandle(freeListSrvIndex_);
    freeListSrvHandle_.second = srvManager_->GetGPUDescriptorHandle(freeListSrvIndex_);
    srvManager_->CreateUAVStructuredBuffer(freeListSrvIndex_, freeListResource_.Get(), settingsData_->maxParticleCount, sizeof(uint32_t));
}

void ParticleCSGroup::CreateSettingsResource() {
    settingsResource_ = dxCommon_->CreateBufferResource(sizeof(ParticleCSSettings));
    settingsResource_->Map(0, nullptr, reinterpret_cast<void **>(&settingsData_));

    // デフォルト設定
    settingsData_->lifeTimeMin = 1.0f;
    settingsData_->lifeTimeMax = 3.0f;
    settingsData_->scaleMin = 0.5f;
    settingsData_->scaleMax = 1.5f;
    settingsData_->velocityMin = {-0.25f, -0.25f, -0.25f};
    settingsData_->velocityMax = {0.25f, 0.25f, 0.25f};
    settingsData_->startColor = {1.0f, 1.0f, 1.0f, 1.0f};
    settingsData_->endColor = {1.0f, 1.0f, 1.0f, 0.0f};
    settingsData_->enableLifetimeScale = 0;
    settingsData_->enableRandomColor = 1;
    settingsData_->enableSinScale = 0;
    settingsData_->sinScaleFrequency = 5.0f;
    settingsData_->sinScaleAmplitude = 0.3f;
    settingsData_->maxParticleCount = 10000;
    settingsData_->emitCount = 0;
    settingsData_->enableGravity = 0;
    settingsData_->gravity = {0.0f, -9.8f, 0.0f};

    // トレイル設定のデフォルト値
    settingsData_->enableTrail = 0;
    settingsData_->trailSpawnDistance = 0.1f; // デフォルトを短く
    settingsData_->maxTrailPerParticle = 5;
    settingsData_->trailLifeTimeScale = 1.0f; // 親と同じ寿命割合に
    settingsData_->trailScaleMultiplier = {0.8f, 0.8f, 0.8f};
    settingsData_->trailColorMultiplier = {1.0f, 1.0f, 1.0f, 0.7f};
    settingsData_->trailVelocityScale = 0.3f;
    settingsData_->trailInheritVelocity = 1;
    settingsData_->trailMinLifeTime = 0.5f; // 最小寿命を長めに

    // ギャザー設定のデフォルト値を追加
    settingsData_->enableGather = 0;
    settingsData_->gatherStartRatio = 0.5f;
    settingsData_->gatherStrength = 2.0f;
    settingsData_->gatherTarget = {0.0f, 0.0f, 0.0f};

    settingsData_->enableAcceleration = 0;
    settingsData_->acceleration = {0.0f, 0.0f, 0.0f};
    settingsData_->enableVelocityDamping = 0;
    settingsData_->velocityDampingFactor = 0.98f;
    settingsData_->enableLifetimeVelocityDamping = 0;
    settingsData_->lifetimeVelocityDampingStart = 0.5f;
    settingsData_->enableRadialVelocity = 0;
    settingsData_->radialVelocityStrength = 1.0f;
    settingsData_->radialVelocityRandomness = 0.2f;
    settingsData_->radialVelocityCenter = {0.0f, 0.0f, 0.0f};

    settingsData_->enableCurlNoise = 0;
    settingsData_->curlNoiseScale = 1.0f;
    settingsData_->curlNoiseStrength = 1.2f;
    settingsData_->curlNoiseTimeScale = 0.2f;
    settingsData_->curlNoiseOctaves = 0;
    settingsData_->curlNoiseAttractStrength = 0.0f;
    settingsData_->curlNoiseBlendMode = 0;
    settingsData_->curlNoisePosRandomStrength = 0.0f;
    settingsData_->curlNoiseAttractCenter = {0.0f, 0.0f, 0.0f};

    // ---- 終了スケール デフォルト ----
    settingsData_->enableEndScale = 0;
    settingsData_->endScaleValue = {0.0f, 0.0f, 0.0f};

    // ---- 回転 デフォルト ----
    settingsData_->enableRandomRotation = 0;
    settingsData_->rotationMin = {0.0f, 0.0f, 0.0f};
    settingsData_->rotationMax = {0.0f, 0.0f, 0.0f};
    settingsData_->enableRandomAngularVelocity = 0;
    settingsData_->angularVelocityMin = {0.0f, 0.0f, 0.0f};
    settingsData_->angularVelocityMax = {0.0f, 0.0f, 0.0f};

    // ---- カラーグラデーション(N段) デフォルト ----
    settingsData_->enableColorGradient = 0;
    // 既定ストップ: 白(不透明) → 白(透明)。enableColorGradient を ON にすると LUT が使われる。
    colorStops_.clear();
    colorStops_.push_back(GradientStop{{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f});
    colorStops_.push_back(GradientStop{{1.0f, 1.0f, 1.0f, 0.0f}, 1.0f});
    colorStopsDirty_ = true;

    // ---- 寿命カーブ(サイズ/アルファ) デフォルト ----
    settingsData_->enableSizeCurve = 0;
    settingsData_->enableAlphaCurve = 0;
    // 既定カーブ: フラット(倍率1.0 = 変化なし)。ON にすると LUT が乗算される。
    sizeCurvePoints_ = {CurvePoint{0.0f, 1.0f}, CurvePoint{1.0f, 1.0f}};
    alphaCurvePoints_ = {CurvePoint{0.0f, 1.0f}, CurvePoint{1.0f, 1.0f}};
    lifeCurvesDirty_ = true;

    // ---- 音声振動 デフォルト ----
    settingsData_->enableAudioVibration = 0;
    settingsData_->audioVibrationStrength = 12.0f;
    settingsData_->audioVibrationSensitivity = 4.0f;
    settingsData_->audioAmplitude = 0.0f;
    settingsData_->audioVibrationFrequency = 22.0f;
    settingsData_->audioAttackSharpness = 1.8f;
    settingsData_->audioReleaseRate = 10.0f;
    settingsData_->audioPad0 = 0.0f;
    audioEnvelope_ = 0.0f;
    audioPrevPeak_ = 0.0f;
}

namespace {
// float RGBA[0,1] → RGBA8 パック（HLSL PackColorRGBA8 と一致: r | g<<8 | b<<16 | a<<24）。
uint32_t PackRGBA8(const Vector4 &c) {
    auto q = [](float v) -> uint32_t {
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<uint32_t>(v * 255.0f + 0.5f);
    };
    return q(c.x) | (q(c.y) << 8) | (q(c.z) << 16) | (q(c.w) << 24);
}
// 寿命カーブ点列（x昇順）を t[0,1] で線形補間する。空なら 1.0（変化なし）。
float SampleCurve(const std::vector<CurvePoint> &pts, float t) {
    if (pts.empty()) return 1.0f;
    if (t <= pts.front().x) return pts.front().y;
    if (t >= pts.back().x) return pts.back().y;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (t <= pts[i].x) {
            float span = pts[i].x - pts[i - 1].x;
            float u = span > 1e-6f ? (t - pts[i - 1].x) / span : 0.0f;
            return pts[i - 1].y + (pts[i].y - pts[i - 1].y) * u;
        }
    }
    return pts.back().y;
}
// 位置でソート済みのストップ列を lifeRatio t[0,1] で線形補間する。
Vector4 SampleGradient(const std::vector<GradientStop> &sorted, float t) {
    if (sorted.empty())
        return {1.0f, 1.0f, 1.0f, 1.0f};
    if (t <= sorted.front().pos)
        return sorted.front().color;
    if (t >= sorted.back().pos)
        return sorted.back().color;
    for (size_t i = 1; i < sorted.size(); ++i) {
        if (t <= sorted[i].pos) {
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

void ParticleCSGroup::BakeColorLUT() {
    // colorStops_ を位置でソートし、256段 RGBA8 LUT を settingsData_->colorLUT にベイクする。
    // ストップ数に依存しない O(256) で、GPU は LUT を1点サンプルするだけになる。
    std::vector<GradientStop> sorted = colorStops_;
    std::sort(sorted.begin(), sorted.end(),
              [](const GradientStop &a, const GradientStop &b) { return a.pos < b.pos; });
    for (int i = 0; i < 256; ++i) {
        float t = static_cast<float>(i) / 255.0f;
        settingsData_->colorLUT[i] = PackRGBA8(SampleGradient(sorted, t));
    }
}

void ParticleCSGroup::BakeLifetimeCurveLUTs() {
    // サイズ/アルファの倍率カーブを 256段 float LUT にベイクする。点が無ければ全 1.0（変化なし）。
    std::vector<CurvePoint> sz = sizeCurvePoints_;
    std::vector<CurvePoint> al = alphaCurvePoints_;
    std::sort(sz.begin(), sz.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
    std::sort(al.begin(), al.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
    for (int i = 0; i < 256; ++i) {
        float t = static_cast<float>(i) / 255.0f;
        settingsData_->sizeCurveLUT[i] = SampleCurve(sz, t);
        settingsData_->alphaCurveLUT[i] = SampleCurve(al, t);
    }
}

void ParticleCSGroup::CreateAliveCountResource() {
    // GPU側のカウント用バッファ (UAV)
    aliveCountResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t), true);

    aliveCountSrvIndex_ = srvManager_->Allocate() + 1;
    aliveCountSrvHandle_.first = srvManager_->GetCPUDescriptorHandle(aliveCountSrvIndex_);
    aliveCountSrvHandle_.second = srvManager_->GetGPUDescriptorHandle(aliveCountSrvIndex_);
    srvManager_->CreateUAVStructuredBuffer(aliveCountSrvIndex_, aliveCountResource_.Get(), 1, sizeof(uint32_t));

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

    dxCommon_->GetDevice()->CreateCommittedResource(
        &readbackHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&aliveCountReadbackResource_));
}

void ParticleCSGroup::CreateAliveListResources() {
    const uint32_t maxCount = settingsData_->maxParticleCount;

    // ping-pong の2枚それぞれに aliveList / aliveCounter を本確保する。
    for (uint32_t i = 0; i < kAlivePingPong; ++i) {
        // --- aliveList: 生存 slot index バッファ (UAV: compute u9 / SRV: VS t2) ---
        aliveListResource_[i] = dxCommon_->CreateBufferResource(sizeof(uint32_t) * maxCount, true);

        aliveListUavIndex_[i] = srvManager_->Allocate() + 1;
        aliveListUavHandle_[i].first = srvManager_->GetCPUDescriptorHandle(aliveListUavIndex_[i]);
        aliveListUavHandle_[i].second = srvManager_->GetGPUDescriptorHandle(aliveListUavIndex_[i]);
        srvManager_->CreateUAVStructuredBuffer(aliveListUavIndex_[i], aliveListResource_[i].Get(), maxCount, sizeof(uint32_t));

        aliveListSrvForVSIndex_[i] = srvManager_->Allocate() + 1;
        srvManager_->CreateSRVforStructuredBuffer(aliveListSrvForVSIndex_[i], aliveListResource_[i].Get(), maxCount, sizeof(uint32_t));

        // --- aliveCounter: 生存数アトミックカウンタ (UAV: compute u10 / SRV: VS t3) ---
        aliveCounterResource_[i] = dxCommon_->CreateBufferResource(sizeof(uint32_t), true);

        aliveCounterUavIndex_[i] = srvManager_->Allocate() + 1;
        aliveCounterUavHandle_[i].first = srvManager_->GetCPUDescriptorHandle(aliveCounterUavIndex_[i]);
        aliveCounterUavHandle_[i].second = srvManager_->GetGPUDescriptorHandle(aliveCounterUavIndex_[i]);
        srvManager_->CreateUAVStructuredBuffer(aliveCounterUavIndex_[i], aliveCounterResource_[i].Get(), 1, sizeof(uint32_t));

        aliveCounterSrvForVSIndex_[i] = srvManager_->Allocate() + 1;
        srvManager_->CreateSRVforStructuredBuffer(aliveCounterSrvForVSIndex_[i], aliveCounterResource_[i].Get(), 1, sizeof(uint32_t));
    }

    // CPU 読み取り用 Readback バッファ（out からコピーする共有 1個）
    D3D12_HEAP_PROPERTIES readbackHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t));
    dxCommon_->GetDevice()->CreateCommittedResource(
        &readbackHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&aliveCounterReadbackResource_));
    aliveCounterReadbackResource_->SetName(L"AliveCounter_Readback");
}

void ParticleCSGroup::CountAliveParticles() {
    // CountParticle.CSを実行
    particleCommon_->ComputeCountDrawCommonSetting();

    commandList_->SetComputeRootConstantBufferView(0, settingsResource_->GetGPUVirtualAddress());
    commandList_->SetComputeRootDescriptorTable(1, aliveCountSrvHandle_.second);
    // SoA: 生存判定は Life バッファ(u1)で行う
    commandList_->SetComputeRootDescriptorTable(2, soaLife_.uavHandle.second);

    int dispatchCount = (settingsData_->maxParticleCount + threadsPerGroup_ - 1) / threadsPerGroup_;
    commandList_->Dispatch(dispatchCount, 1, 1);

    // UAVバリア（UAV書き込み完了を保証）
    dxCommon_->TransitionUAVBarrier(aliveCountResource_.Get());

    // CopyResource前にリソース状態を遷移（自作関数を使う）
    dxCommon_->BarrierTransition(
        aliveCountResource_.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    // GPU→CPUへコピー
    commandList_->CopyResource(aliveCountReadbackResource_.Get(), aliveCountResource_.Get());

    // 戻す（次のDispatch用に再びUAV状態へ）
    dxCommon_->BarrierTransition(
        aliveCountResource_.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

uint32_t ParticleCSGroup::GetAliveParticleCount() {
    // 旧 CountParticle 全Nディスパッチを廃止し、生存コンパクションの
    // aliveCounter 読み戻し値(FetchAliveDrawCount で更新)をそのまま統計に流用する。
    cachedAliveCount_ = aliveDrawCount_;
    return cachedAliveCount_;
}

#ifdef USE_IMGUI
namespace {
// ImGradient ウィジェット用デリゲート。GradientStop 列(RGBA+位置)を
// ImVec4(xyz=RGB, w=位置) のスクラッチ配列を介して編集する（2Dエンジンの ColorGradient と同型）。
struct ColorGradientDelegate : public ImGradient::Delegate {
    std::vector<Hagine::GradientStop> *stops = nullptr;
    std::vector<ImVec4> scratch;
    std::vector<Hagine::GradientStop> sorted;
    void Sync() {
        if (!stops) { scratch.clear(); sorted.clear(); return; }
        scratch.resize(stops->size());
        for (size_t i = 0; i < stops->size(); ++i) {
            const auto &s = (*stops)[i];
            scratch[i] = ImVec4(s.color.x, s.color.y, s.color.z, s.pos);
        }
        sorted = *stops;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Hagine::GradientStop &a, const Hagine::GradientStop &b) { return a.pos < b.pos; });
    }
    Hagine::Vector4 Sample(float t) const {
        if (sorted.empty()) return {1.0f, 1.0f, 1.0f, 1.0f};
        if (t <= sorted.front().pos) return sorted.front().color;
        if (t >= sorted.back().pos) return sorted.back().color;
        for (size_t i = 1; i < sorted.size(); ++i) {
            if (t <= sorted[i].pos) {
                const auto &a = sorted[i - 1].color;
                const auto &b = sorted[i].color;
                float span = sorted[i].pos - sorted[i - 1].pos;
                float u = span > 1e-6f ? (t - sorted[i - 1].pos) / span : 0.0f;
                return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
                        a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u};
            }
        }
        return sorted.back().color;
    }
    size_t GetPointCount() override { return stops ? stops->size() : 0; }
    ImVec4 *GetPoints() override { return scratch.data(); }
    int EditPoint(int index, ImVec4 value) override {
        if (!stops || index < 0 || index >= static_cast<int>(stops->size())) return index;
        float p = value.w; p = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
        (*stops)[index].pos = p;
        (*stops)[index].color.x = value.x;
        (*stops)[index].color.y = value.y;
        (*stops)[index].color.z = value.z;
        if (index < static_cast<int>(scratch.size())) scratch[index] = ImVec4(value.x, value.y, value.z, p);
        return index;
    }
    ImVec4 GetPoint(float t) override { Hagine::Vector4 c = Sample(t); return ImVec4(c.x, c.y, c.z, t); }
    void AddPoint(ImVec4 value) override {
        if (!stops) return;
        Hagine::GradientStop s;
        s.pos = value.w;
        Hagine::Vector4 sampled = Sample(value.w); // アルファは既存グラデから補間して引き継ぐ
        s.color = {value.x, value.y, value.z, sampled.w};
        stops->push_back(s);
        Sync();
    }
};

// ImCurveEdit 用デリゲート。サイズ(0)/アルファ(1) の倍率カーブを1つのエディタで編集する。
// 点の実体は group の sizeCurvePoints_/alphaCurvePoints_(CurvePoint列)。ImVec2 スクラッチ経由で編集する。
struct LifetimeCurvesDelegate : public ImCurveEdit::Delegate {
    std::vector<Hagine::CurvePoint> *pts[2] = {nullptr, nullptr}; // 0=size, 1=alpha
    std::vector<ImVec2> scratch[2];
    bool visible[2] = {true, true};
    bool changed = false; // この Edit 呼び出しで点が編集されたか（dirty 判定用）
    ImVec2 vmin = ImVec2(0.0f, 0.0f);
    ImVec2 vmax = ImVec2(1.0f, 2.0f);
    void Sync() {
        for (int c = 0; c < 2; ++c) {
            scratch[c].clear();
            if (pts[c])
                for (const auto &p : *pts[c]) scratch[c].push_back(ImVec2(p.x, p.y));
        }
    }
    size_t GetCurveCount() override { return 2; }
    bool IsVisible(size_t c) override { return c < 2 ? visible[c] : true; }
    ImCurveEdit::CurveType GetCurveType(size_t) const override { return ImCurveEdit::CurveLinear; }
    ImVec2 &GetMin() override { return vmin; }
    ImVec2 &GetMax() override { return vmax; }
    size_t GetPointCount(size_t c) override { return (c < 2 && pts[c]) ? pts[c]->size() : 0; }
    uint32_t GetCurveColor(size_t c) override { return c == 0 ? 0xFF3399FF : 0xFFFFCC66; } // size=橙 / alpha=水(ABGR)
    ImVec2 *GetPoints(size_t c) override { return c < 2 ? scratch[c].data() : nullptr; }
    int EditPoint(size_t c, int index, ImVec2 value) override {
        if (c >= 2 || !pts[c] || index < 0 || index >= static_cast<int>(pts[c]->size())) return index;
        value.x = value.x < 0.0f ? 0.0f : (value.x > 1.0f ? 1.0f : value.x);
        if (value.y < 0.0f) value.y = 0.0f;
        (*pts[c])[index] = {value.x, value.y};
        scratch[c][index] = value;
        changed = true;
        while (index > 0 && (*pts[c])[index].x < (*pts[c])[index - 1].x) {
            std::swap((*pts[c])[index], (*pts[c])[index - 1]);
            std::swap(scratch[c][index], scratch[c][index - 1]);
            --index;
        }
        while (index < static_cast<int>(pts[c]->size()) - 1 && (*pts[c])[index].x > (*pts[c])[index + 1].x) {
            std::swap((*pts[c])[index], (*pts[c])[index + 1]);
            std::swap(scratch[c][index], scratch[c][index + 1]);
            ++index;
        }
        return index;
    }
    void AddPoint(size_t c, ImVec2 value) override {
        if (c >= 2 || !pts[c]) return;
        value.x = value.x < 0.0f ? 0.0f : (value.x > 1.0f ? 1.0f : value.x);
        if (value.y < 0.0f) value.y = 0.0f;
        pts[c]->push_back({value.x, value.y});
        std::sort(pts[c]->begin(), pts[c]->end(),
                  [](const Hagine::CurvePoint &a, const Hagine::CurvePoint &b) { return a.x < b.x; });
        changed = true;
        Sync();
    }
};
} // namespace
#endif

void ParticleCSGroup::DrawImGui() {
#ifdef USE_IMGUI
    if (!settingsData_)
        return;

    auto PushSectionColor = [](ImVec4 col) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(col.x * 0.45f, col.y * 0.45f, col.z * 0.45f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(col.x * 0.55f, col.y * 0.55f, col.z * 0.55f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(col.x * 0.65f, col.y * 0.65f, col.z * 0.65f, 0.85f));
    };
    auto PopSectionColor = []() { ImGui::PopStyleColor(3); };

    ImGui::PushItemWidth(-120.0f);

    // エフェクトを「コア（常設）＋ 追加したものだけのカード」で構成する。
    // 各エフェクトカードのヘッダ（× 削除ボタン付き）。展開中かどうかを返す。
    // onRemove で enable フラグを 0 にすると、そのエフェクトは非表示になり「＋追加」リストへ戻る。
    auto effectHeader = [&](const char *label, ImVec4 col, const std::function<void()> &onRemove) -> bool {
        ImGui::PushID(label);
        PushSectionColor(col);
        // AllowOverlap: 後続の × ボタンをヘッダに重ねてもクリックがボタン側に渡るようにする
        // （これが無いとヘッダが全幅でクリックを奪い、× が押せず開閉だけになる）。
        bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        PopSectionColor();
        // ヘッダ右端に × 削除ボタン（ヘッダに重ねて配置）
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 28.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.2f, 0.2f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.3f, 0.3f, 1.0f));
        if (ImGui::SmallButton("✕"))
            onRemove();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("このエフェクトを削除");
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        return open;
    };

    // =======================================================
    // 1. 出現・寿命・サイズ（赤系）【コア・常設】
    // =======================================================
    PushSectionColor(ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
    bool openBasic = ImGui::CollapsingHeader("  出現 / 寿命 / サイズ");
    PopSectionColor();
    if (openBasic) {
        ImGui::Indent();

        // 出現数
        {
            int emitCount = static_cast<int>(settingsData_->emitCount);
            int dynMax = CalculateOptimalEmitCount();
            int absMax = static_cast<int>(settingsData_->maxParticleCount);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4f, 0.15f, 0.15f, 0.5f));
            if (ImGui::DragInt("出現数", &emitCount, 1, 0, 100000))
                settingsData_->emitCount = static_cast<uint32_t>(emitCount);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("推奨上限: %d  /  絶対上限: %d", dynMax, absMax);
            if (emitCount > dynMax) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
                ImGui::TextUnformatted(" 推奨超過");
                ImGui::PopStyleColor();
            }
        }

        // 寿命（横並び）
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4f, 0.15f, 0.15f, 0.5f));
            float hw = (ImGui::GetContentRegionAvail().x - 130.0f) * 0.5f - 4.0f;
            ImGui::SetNextItemWidth(hw);
            ImGui::DragFloat("##lifeMin", &settingsData_->lifeTimeMin, 0.1f, 0.0f, 9999.0f, "Min %.4fs");
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(hw);
            ImGui::DragFloat("##lifeMax", &settingsData_->lifeTimeMax, 0.1f, 0.0f, 9999.0f, "Max %.4fs");
            ImGui::SameLine();
            ImGui::TextUnformatted("寿命(s)");
            ImGui::PopStyleColor();
        }

        // サイズ（横並び）
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4f, 0.3f, 0.1f, 0.5f));
            float hw = (ImGui::GetContentRegionAvail().x - 130.0f) * 0.5f - 4.0f;
            ImGui::SetNextItemWidth(hw);
            ImGui::DragFloat("##scMin", &settingsData_->scaleMin, 0.01f, 0.0f, 9999.0f, "Min %.4f");
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(hw);
            ImGui::DragFloat("##scMax", &settingsData_->scaleMax, 0.01f, 0.0f, 9999.0f, "Max %.4f");
            ImGui::SameLine();
            ImGui::TextUnformatted("サイズ");
            ImGui::PopStyleColor();
        }

        ImGui::Unindent();
    }

    // =======================================================
    // 2. 速度・色彩・ブレンド（青系）
    // =======================================================
    PushSectionColor(ImVec4(0.25f, 0.45f, 0.8f, 1.0f));
    bool openAppearance = ImGui::CollapsingHeader("  速度 / 色彩 / ブレンド");
    PopSectionColor();
    if (openAppearance) {
        ImGui::Indent();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.2f, 0.4f, 0.5f));
        ImGui::DragFloat3("速度 Min", &settingsData_->velocityMin.x, 0.01f, -9999.0f, 9999.0f, "%.4f");
        ImGui::DragFloat3("速度 Max", &settingsData_->velocityMax.x, 0.01f, -9999.0f, 9999.0f, "%.4f");
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // 色彩
        {
            // グラデーション(多段) モード — ON で寿命に沿った N段カラーを使う（既存の3段/ランダムを上書き）
            bool grad = settingsData_->enableColorGradient != 0;
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
            if (ImGui::Checkbox("グラデーション(多段)", &grad)) {
                settingsData_->enableColorGradient = grad ? 1 : 0;
                MarkColorStopsDirty();
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("寿命に沿った多段カラーグラデーション\nバー上ダブルクリックで色追加 / 点ドラッグで移動 / 選択して色・削除");

            if (grad) {
                // ===== ImGradient エディタ（連続プレビューバー + ストップ編集） =====
                ColorGradientDelegate dg;
                dg.stops = &colorStops_;
                dg.Sync();
                // 連続グラデーションのプレビューバー（RGBのみ。アルファはフェードとして別途効く）
                {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    float barW = ImGui::GetContentRegionAvail().x;
                    const float barH = 16.0f;
                    const int kSteps = 64;
                    for (int i = 0; i < kSteps; ++i) {
                        float t0 = static_cast<float>(i) / kSteps;
                        float t1 = static_cast<float>(i + 1) / kSteps;
                        Vector4 c0 = dg.Sample(t0);
                        Vector4 c1 = dg.Sample(t1);
                        ImU32 u0 = ImGui::ColorConvertFloat4ToU32(ImVec4(c0.x, c0.y, c0.z, 1.0f));
                        ImU32 u1 = ImGui::ColorConvertFloat4ToU32(ImVec4(c1.x, c1.y, c1.z, 1.0f));
                        dl->AddRectFilledMultiColor(ImVec2(p0.x + barW * t0, p0.y),
                                                    ImVec2(p0.x + barW * t1, p0.y + barH), u0, u1, u1, u0);
                    }
                    ImGui::Dummy(ImVec2(barW, barH));
                }
                int sel = -1;
                if (ImGradient::Edit(dg, ImVec2(ImGui::GetContentRegionAvail().x, 40.0f), sel))
                    MarkColorStopsDirty();
                ImGui::TextDisabled("点ドラッグ=移動 / バー上ダブルクリック=追加");
                if (sel >= 0 && sel < static_cast<int>(colorStops_.size())) {
                    if (ImGui::ColorEdit4("ストップ RGBA##grad", &colorStops_[sel].color.x))
                        MarkColorStopsDirty();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("削除##gradStop") && colorStops_.size() > 1) {
                        colorStops_.erase(colorStops_.begin() + sel);
                        MarkColorStopsDirty();
                    }
                } else {
                    ImGui::TextDisabled("(ストップ未選択 — バー上の点をクリックで選択)");
                }
                // プリセット
                ImGui::TextDisabled("プリセット:");
                ImGui::SameLine();
                if (ImGui::SmallButton("炎##gradPre1")) {
                    colorStops_ = {{{1.0f, 1.0f, 0.6f, 1.0f}, 0.0f}, {{1.0f, 0.55f, 0.1f, 1.0f}, 0.35f}, {{0.9f, 0.12f, 0.0f, 0.6f}, 0.75f}, {{0.3f, 0.0f, 0.0f, 0.0f}, 1.0f}};
                    MarkColorStopsDirty();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("虹##gradPre2")) {
                    colorStops_ = {{{1.0f, 0.3f, 0.4f, 1.0f}, 0.0f}, {{0.3f, 0.8f, 1.0f, 1.0f}, 0.33f}, {{1.0f, 0.9f, 0.3f, 1.0f}, 0.66f}, {{0.5f, 1.0f, 0.5f, 0.0f}, 1.0f}};
                    MarkColorStopsDirty();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("魔法##gradPre3")) {
                    colorStops_ = {{{0.8f, 0.4f, 1.0f, 1.0f}, 0.0f}, {{0.4f, 0.7f, 1.0f, 1.0f}, 0.45f}, {{1.0f, 0.9f, 1.0f, 0.7f}, 0.8f}, {{0.6f, 0.4f, 1.0f, 0.0f}, 1.0f}};
                    MarkColorStopsDirty();
                }
            } else {
            bool rnd = settingsData_->enableRandomColor != 0;
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
            if (ImGui::Checkbox("ランダムカラー", &rnd))
                settingsData_->enableRandomColor = rnd ? 1 : 0;
            ImGui::PopStyleColor();
            if (!rnd) {
                ImGui::ColorEdit4("開始色", &settingsData_->startColor.x);
                // 中間色
                {
                    bool mc = settingsData_->enableMidColor != 0;
                    if (ImGui::Checkbox("中間色を有効化##mc", &mc))
                        settingsData_->enableMidColor = mc ? 1 : 0;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("開始→中間→終了の3段階カラーグラデーション");
                    if (mc) {
                        ImGui::Indent();
                        ImGui::ColorEdit4("中間色##mcc", &settingsData_->midColor.x);
                        ImGui::DragFloat("中間タイミング##mcr", &settingsData_->midColorRatio, 0.01f, 0.0f, 1.0f, "%.2f");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("中間色に達するlife比率\n0=開始直後 / 0.5=寿命半分 / 1=終了直前");
                        ImGui::Spacing();
                        ImGui::TextDisabled("プリセット:");
                        ImGui::SameLine();
                        if (ImGui::SmallButton("炎##mcPre1")) {
                            settingsData_->startColor  = {1.0f, 0.3f, 0.0f, 1.0f};
                            settingsData_->midColor    = {1.0f, 1.0f, 0.3f, 1.0f};
                            settingsData_->endColor    = {0.2f, 0.2f, 0.2f, 0.0f};
                            settingsData_->midColorRatio = 0.35f;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("魔法陣##mcPre2")) {
                            settingsData_->startColor  = {0.2f, 0.5f, 1.0f, 0.0f};
                            settingsData_->midColor    = {1.0f, 1.0f, 1.0f, 1.0f};
                            settingsData_->endColor    = {0.5f, 0.2f, 1.0f, 0.0f};
                            settingsData_->midColorRatio = 0.5f;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("雷##mcPre3")) {
                            settingsData_->startColor  = {1.0f, 1.0f, 1.0f, 1.0f};
                            settingsData_->midColor    = {0.7f, 0.9f, 1.0f, 0.8f};
                            settingsData_->endColor    = {0.2f, 0.4f, 0.8f, 0.0f};
                            settingsData_->midColorRatio = 0.4f;
                        }
                        ImGui::Unindent();
                    }
                }
                ImGui::ColorEdit4("終了色", &settingsData_->endColor.x);
            } else {
                // ランダムカラー時はRGBがランダムのため色編集は非表示にするが、
                // アルファ（透明度）は startColor.a / endColor.a で補間されるので個別に編集できるようにする
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.2f, 0.4f, 0.5f));
                ImGui::DragFloat("開始アルファ##rndAlphaStart", &settingsData_->startColor.w, 0.01f, 0.0f, 1.0f, "%.4f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("発生時の透明度 (0=完全透明, 1=完全不透明)");
                ImGui::DragFloat("終了アルファ##rndAlphaEnd", &settingsData_->endColor.w, 0.01f, 0.0f, 1.0f, "%.4f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("消滅時の透明度 (0=完全透明, 1=完全不透明)");
                ImGui::PopStyleColor();
            }
            } // else: グラデーション(多段) OFF
        }

        ImGui::Spacing();

        // ブレンドモード
        {
            const char *blendNames[] = {"なし", "通常", "加算", "減算", "乗算", "スクリーン"};
            int bm = static_cast<int>(particleGroupData_.blendMode);
            if (ImGui::Combo("ブレンドモード", &bm, blendNames, IM_ARRAYSIZE(blendNames)))
                particleGroupData_.blendMode = static_cast<BlendMode>(bm);
        }

        ImGui::Unindent();
    }

    // =======================================================
    // 2.5 テクスチャ（画像差し替え・水色系）
    // =======================================================
    PushSectionColor(ImVec4(0.4f, 0.7f, 0.75f, 1.0f));
    bool openTex = ImGui::CollapsingHeader("  テクスチャ");
    PopSectionColor();
    if (openTex && !particleGroupData_.materials.empty()) {
        ImGui::Indent();
        // Application/Assets/images 配下の画像を列挙（初回スキャン + 再スキャンボタン）。
        // textureFilePath は base からの相対パス('/'区切り)で持つ規約に合わせる。
        static std::vector<std::string> s_imageFiles;
        static bool s_scanned = false;
        auto scanImages = []() {
            s_imageFiles.clear();
            std::error_code ec;
            // images はエンジン(debug)とアプリの 2 ルートに分割されているため両方を走査する。
            for (const std::string &base : AssetPath::ImageScanRoots()) {
                if (!std::filesystem::exists(base, ec))
                    continue;
                for (auto &e : std::filesystem::recursive_directory_iterator(base, ec)) {
                    if (ec)
                        break;
                    if (!e.is_regular_file())
                        continue;
                    std::string ext = e.path().extension().string();
                    for (auto &ch : ext)
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".dds")
                        continue;
                    std::string rel = std::filesystem::relative(e.path(), base, ec).generic_string();
                    if (!rel.empty())
                        s_imageFiles.push_back(rel);
                }
            }
            std::sort(s_imageFiles.begin(), s_imageFiles.end());
        };
        if (!s_scanned) {
            scanImages();
            s_scanned = true;
        }

        std::string &curPath = particleGroupData_.materials[0].textureFilePath;

        // テクスチャを差し替えて全マテリアルへ反映するヘルパー。
        // 描画は毎フレーム textureFilePath で引くので、パス差し替え + LoadTexture で即時反映される。
        auto applyTexture = [&](const std::string &path) {
            SetTexture(path);
        };

        // 選択中テクスチャのサムネイルプレビュー（読み込み済み前提）。
        // ※ GetSrvHandleGPU は他の getter と違い相対パスを前置しない＝フルパス(＝マップキー)を要求する。
        if (!curPath.empty()) {
            texManager_->LoadTexture(curPath); // 念のため未ロードならロード（ロード済みなら即return）
            // キューブマップは SRV が TEXTURECUBE。Texture2D として Image 描画すると
            // GPU ベース検証 #940 で落ちるためプレビューしない。
            if (texManager_->GetMetaData(curPath).IsCubemap()) {
                ImGui::Button("CUBE", ImVec2(56.0f, 56.0f));
            } else {
                D3D12_GPU_DESCRIPTOR_HANDLE h = texManager_->GetSrvHandleGPU(AssetPath::Image(curPath));
                if (h.ptr != 0)
                    ImGui::Image(static_cast<ImTextureID>(h.ptr), ImVec2(56.0f, 56.0f));
                else
                    ImGui::Button("画像\nなし", ImVec2(56.0f, 56.0f));
            }
        } else {
            // 未設定。アセットブラウザからのドロップ先となるプレースホルダ。
            ImGui::Button("ここへ\nドロップ", ImVec2(56.0f, 56.0f));
        }
        // サムネ（またはプレースホルダ）をアセットブラウザからのドロップ先にする。
        {
            std::string dropped;
            if (AssetDragDrop::TextureTarget(dropped))
                applyTexture(dropped);
        }
        ImGui::SameLine();

        ImGui::BeginGroup();
        if (ImGui::BeginCombo("画像", curPath.c_str())) {
            for (const std::string &f : s_imageFiles) {
                bool sel = (f == curPath);
                if (ImGui::Selectable(f.c_str(), sel))
                    applyTexture(f); // パスを差し替え（毎フレーム path 参照なので即時反映）
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        // コンボもドロップ先にする。
        {
            std::string dropped;
            if (AssetDragDrop::TextureTarget(dropped))
                applyTexture(dropped);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("再スキャン"))
            scanImages();
        ImGui::TextDisabled("画像から選択 / アセットブラウザからのD&Dでも設定可");
        ImGui::EndGroup();
        ImGui::Unindent();
    }

    // ビルボード（コア・常設）
    ImGui::Spacing();
    {
        bool v = perViewData_->enableBillboard != 0;
        if (ImGui::Checkbox("ビルボード（常にカメラを向く）", &v))
            perViewData_->enableBillboard = v ? 1 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ONでパーティクルが常にカメラ正面を向きます\nOFFにするとワールド空間に固定されます");
    }

    // =======================================================
    // エフェクト（追加したものだけカード表示）
    // =======================================================
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
    ImGui::TextUnformatted("エフェクト");
    ImGui::PopStyleColor();
    {
        // 「＋追加」リスト。各エフェクトの「追加済みか」と「追加アクション」を列挙する。
        // グループ系（描画カリング/寿命カーブ/回転）は内部フラグのいずれかが立っていれば追加済み扱い。
        struct AddItem {
            const char *name;
            bool added;
            std::function<void()> add;
        };
        std::vector<AddItem> addItems = {
            {"重力", settingsData_->enableGravity != 0, [&] { settingsData_->enableGravity = 1; }},
            {"加速度", settingsData_->enableAcceleration != 0, [&] { settingsData_->enableAcceleration = 1; }},
            {"速度減衰", settingsData_->enableVelocityDamping != 0, [&] { settingsData_->enableVelocityDamping = 1; }},
            {"寿命による速度減衰", settingsData_->enableLifetimeVelocityDamping != 0, [&] { settingsData_->enableLifetimeVelocityDamping = 1; }},
            {"寿命で縮小", settingsData_->enableLifetimeScale != 0, [&] { settingsData_->enableLifetimeScale = 1; }},
            {"Sin波で拡縮", settingsData_->enableSinScale != 0, [&] { settingsData_->enableSinScale = 1; }},
            {"速度ストレッチ", perViewData_->enableVelocityStretch != 0, [&] { perViewData_->enableVelocityStretch = 1; }},
            {"描画カリング", (perViewData_->enableDistanceCull || perViewData_->enableSizeClamp) != 0, [&] { perViewData_->enableDistanceCull = 1; }},
            {"寿命カーブ", (settingsData_->enableSizeCurve || settingsData_->enableAlphaCurve) != 0, [&] { settingsData_->enableSizeCurve = 1; MarkLifeCurvesDirty(); }},
            {"タービュランス", settingsData_->enableTurbulence != 0, [&] { settingsData_->enableTurbulence = 1; }},
            {"音声振動", settingsData_->enableAudioVibration != 0, [&] { settingsData_->enableAudioVibration = 1; }},
            {"終了スケール", settingsData_->enableEndScale != 0, [&] { settingsData_->enableEndScale = 1; }},
            {"回転", (settingsData_->enableRandomRotation || settingsData_->enableRandomAngularVelocity) != 0, [&] { settingsData_->enableRandomRotation = 1; }},
            {"放射状速度", settingsData_->enableRadialVelocity != 0, [&] { settingsData_->enableRadialVelocity = 1; }},
            {"ギャザー", settingsData_->enableGather != 0, [&] { settingsData_->enableGather = 1; }},
            {"渦巻き", settingsData_->enableVortex != 0, [&] { settingsData_->enableVortex = 1; }},
            {"カールノイズ", settingsData_->enableCurlNoise != 0, [&] { settingsData_->enableCurlNoise = 1; }},
            {"トレイル", settingsData_->enableTrail != 0, [&] { settingsData_->enableTrail = 1; }},
        };
        int notAdded = 0;
        for (const auto &it : addItems)
            if (!it.added)
                ++notAdded;
        ImGui::SetNextItemWidth(-1.0f);
        const char *preview = notAdded > 0 ? "＋ エフェクトを追加..." : "（すべて追加済み）";
        if (ImGui::BeginCombo("##addEffect", preview)) {
            for (const auto &it : addItems) {
                if (it.added)
                    continue;
                if (ImGui::Selectable(it.name))
                    it.add();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Spacing();

    const ImVec4 kMotionColor = ImVec4(0.5f, 0.75f, 0.2f, 1.0f);

    // ---- 寿命で縮小 ----
    if (settingsData_->enableLifetimeScale) {
        if (effectHeader("寿命で縮小", kMotionColor, [&] { settingsData_->enableLifetimeScale = 0; })) {
            ImGui::Indent();
            ImGui::TextDisabled("時間経過と共にスケールが 0 に近づきます（パラメータなし）");
            ImGui::Unindent();
        }
    }

    // ---- Sin波で拡縮 ----
    if (settingsData_->enableSinScale) {
        if (effectHeader("Sin波で拡縮", kMotionColor, [&] { settingsData_->enableSinScale = 0; })) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.35f, 0.15f, 0.5f));
            ImGui::DragFloat("周波数##sf", &settingsData_->sinScaleFrequency, 0.1f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat("振幅##sa", &settingsData_->sinScaleAmplitude, 0.01f, 0.0f, 999.0f, "%.4f");
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
    }

    // ---- 重力 ----
    if (settingsData_->enableGravity) {
        if (effectHeader("重力", kMotionColor, [&] { settingsData_->enableGravity = 0; })) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.35f, 0.15f, 0.5f));
            ImGui::DragFloat3("重力ベクトル", &settingsData_->gravity.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
    }

    // ---- 加速度 ----
    if (settingsData_->enableAcceleration) {
        if (effectHeader("加速度", kMotionColor, [&] { settingsData_->enableAcceleration = 0; })) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.35f, 0.15f, 0.5f));
            ImGui::DragFloat3("加速度ベクトル", &settingsData_->acceleration.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();
            ImGui::TextDisabled("重力とは別に毎フレーム速度に加算されます");
            ImGui::Unindent();
        }
    }

    // ---- 速度減衰 ----
    if (settingsData_->enableVelocityDamping) {
        if (effectHeader("速度減衰", kMotionColor, [&] { settingsData_->enableVelocityDamping = 0; })) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.35f, 0.15f, 0.5f));
            ImGui::DragFloat("減衰係数##vd", &settingsData_->velocityDampingFactor, 0.001f, 0.0f, 1.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("空気抵抗のように徐々に減速します\n推奨: 0.95-0.99");
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
    }

    // ---- 寿命による速度減衰 ----
    if (settingsData_->enableLifetimeVelocityDamping) {
        if (effectHeader("寿命による速度減衰", kMotionColor, [&] { settingsData_->enableLifetimeVelocityDamping = 0; })) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.35f, 0.15f, 0.5f));
            ImGui::DragFloat("開始タイミング##ld", &settingsData_->lifetimeVelocityDampingStart, 0.01f, 0.0f, 1.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("寿命末期に速度が 0 に近づきます\n0.0=最初から / 1.0=最後のみ / 推奨: 0.5-0.8");
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
    }

    // ---- 速度ストレッチ ----
    if (perViewData_->enableVelocityStretch) {
        if (effectHeader("速度ストレッチ", kMotionColor, [&] { perViewData_->enableVelocityStretch = 0; })) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.35f, 0.15f, 0.5f));
            ImGui::DragFloat("ストレッチ係数##vsf", &perViewData_->velocityStretchFactor, 0.01f, 0.0f, 10.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("パーティクルを速度方向に引き伸ばします\n速さ × 係数 = 伸び率 / 推奨: 0.05〜0.5");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("火花##vsPre1")) {
                perViewData_->enableVelocityStretch = 1;
                perViewData_->velocityStretchFactor = 0.15f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("銃弾##vsPre2")) {
                perViewData_->enableVelocityStretch = 1;
                perViewData_->velocityStretchFactor = 0.5f;
            }
            ImGui::Unindent();
        }
    }

    // ---- 描画カリング（距離カリング / 画面サイズ制限）----
    if ((perViewData_->enableDistanceCull || perViewData_->enableSizeClamp) &&
        effectHeader("描画カリング（overdraw対策）", ImVec4(0.3f, 0.7f, 0.8f, 1.0f),
                     [&] { perViewData_->enableDistanceCull = 0; perViewData_->enableSizeClamp = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.4f, 0.8f, 0.9f, 1.0f));

        // 距離カリング + 距離フェード（遠い粒子のフィルレートを節約）
        {
            bool v = perViewData_->enableDistanceCull != 0;
            if (ImGui::Checkbox("距離カリング##dc", &v))
                perViewData_->enableDistanceCull = v ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("遠い粒子をアルファフェード→縮退カリングして\n半透明の重なり(ROP/blend)を減らします");
            if (v) {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.3f, 0.35f, 0.5f));
                ImGui::DragFloat("フェード開始距離##dcs", &perViewData_->distanceCullStart, 0.5f, 0.0f, 100000.0f, "%.2f");
                ImGui::DragFloat("カリング距離##dce", &perViewData_->distanceCullEnd, 0.5f, 0.0f, 100000.0f, "%.2f");
                ImGui::PopStyleColor();
                // 開始 <= カリング距離 を保証（フェード範囲が負にならないように）
                if (perViewData_->distanceCullEnd < perViewData_->distanceCullStart)
                    perViewData_->distanceCullEnd = perViewData_->distanceCullStart;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("開始距離からアルファをフェードし、カリング距離で完全に消えます\nカメラからの距離(ワールド単位)");
                ImGui::Unindent();
            }
        }

        ImGui::Spacing();

        // 画面サイズ上限 + 微小カリング
        {
            bool v = perViewData_->enableSizeClamp != 0;
            if (ImGui::Checkbox("画面サイズ制限##sc", &v))
                perViewData_->enableSizeClamp = v ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("巨大粒子のサイズを画面上で上限クランプし、\nサブピクセル粒子を破棄してフィルレートを節約します");
            if (v) {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.3f, 0.35f, 0.5f));
                ImGui::DragFloat("最大画面高さ##scmax", &perViewData_->maxScreenHeight, 0.01f, 0.01f, 2.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("画面上の最大高さ(NDC)。2.0=画面全体, 1.0=画面の半分\nこれを超える巨大粒子はスケールを縮小します");
                ImGui::DragFloat("微小カリング高さ##scmin", &perViewData_->minScreenHeight, 0.0005f, 0.0f, 0.5f, "%.4f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("画面上の高さがこれ未満の粒子を破棄(0=無効)\n例: 0.002 ≒ 1080pで約2px");
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- 寿命カーブ（サイズ/アルファ。1つのカーブエディタを共有）----
    if ((settingsData_->enableSizeCurve || settingsData_->enableAlphaCurve) &&
        effectHeader("寿命カーブ（サイズ/アルファ）", ImVec4(0.6f, 0.45f, 0.8f, 1.0f),
                     [&] { settingsData_->enableSizeCurve = 0; settingsData_->enableAlphaCurve = 0; MarkLifeCurvesDirty(); })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.8f, 0.6f, 0.95f, 1.0f));

        bool sizeOn = settingsData_->enableSizeCurve != 0;
        if (ImGui::Checkbox("サイズ倍率##szc", &sizeOn)) {
            settingsData_->enableSizeCurve = sizeOn ? 1 : 0;
            MarkLifeCurvesDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("寿命に沿ってサイズを倍率(0〜2)で変化させる\n例: 0→大きく→0 でポップ感");
        ImGui::SameLine();
        bool alphaOn = settingsData_->enableAlphaCurve != 0;
        if (ImGui::Checkbox("アルファ倍率##alc", &alphaOn)) {
            settingsData_->enableAlphaCurve = alphaOn ? 1 : 0;
            MarkLifeCurvesDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("寿命に沿って不透明度を倍率で変化させる\n例: フェードイン→アウト");

        if (sizeOn || alphaOn) {
            LifetimeCurvesDelegate dg;
            dg.pts[0] = &sizeCurvePoints_;
            dg.pts[1] = &alphaCurvePoints_;
            dg.visible[0] = sizeOn;
            dg.visible[1] = alphaOn;
            dg.Sync();
            // 凡例 + リセット
            ImGui::ColorButton("##lcS", ImVec4(1.0f, 0.6f, 0.2f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
            ImGui::SameLine(); ImGui::TextUnformatted("サイズ");
            ImGui::SameLine();
            ImGui::ColorButton("##lcA", ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
            ImGui::SameLine(); ImGui::TextUnformatted("アルファ");
            ImGui::SameLine();
            if (ImGui::SmallButton("リセット##lcReset")) {
                sizeCurvePoints_ = {{0.0f, 1.0f}, {1.0f, 1.0f}};
                alphaCurvePoints_ = {{0.0f, 1.0f}, {1.0f, 1.0f}};
                MarkLifeCurvesDirty();
            }
            ImCurveEdit::Edit(dg, ImVec2(ImGui::GetContentRegionAvail().x, 140.0f), 7321);
            if (dg.changed)
                MarkLifeCurvesDirty();
            ImGui::TextDisabled("点ドラッグ=移動 / 線上ダブルクリック=追加 / ホイール=Y拡縮");
            // プリセット
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("ポップ##lcP1")) {
                sizeCurvePoints_ = {{0.0f, 0.0f}, {0.2f, 1.2f}, {1.0f, 0.0f}};
                alphaCurvePoints_ = {{0.0f, 0.0f}, {0.1f, 1.0f}, {1.0f, 0.0f}};
                MarkLifeCurvesDirty();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("煙##lcP2")) {
                sizeCurvePoints_ = {{0.0f, 0.3f}, {1.0f, 1.0f}};
                alphaCurvePoints_ = {{0.0f, 0.0f}, {0.25f, 1.0f}, {1.0f, 0.0f}};
                MarkLifeCurvesDirty();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("フェード##lcP3")) {
                alphaCurvePoints_ = {{0.0f, 0.0f}, {0.15f, 1.0f}, {0.85f, 1.0f}, {1.0f, 0.0f}};
                MarkLifeCurvesDirty();
            }
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- タービュランス ----
    if (settingsData_->enableTurbulence &&
        effectHeader("タービュランス（振動力）", ImVec4(0.9f, 0.55f, 0.1f, 1.0f),
                     [&] { settingsData_->enableTurbulence = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));

        bool v = settingsData_->enableTurbulence != 0;

        if (v) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4f, 0.25f, 0.05f, 0.5f));
            ImGui::DragFloat("振動強度##tbs", &settingsData_->turbulenceStrength, 0.05f, 0.0f, 50.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("大きいほど激しく揺れます\n推奨: 0.5〜5.0");
            ImGui::DragFloat("振動周波数##tbf", &settingsData_->turbulenceFrequency, 0.1f, 0.0f, 30.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("周波数 (Hz) — 大きいほど細かく素早く振動\n推奨: 1〜8");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("ゆらめき##tbPre1")) {
                settingsData_->turbulenceStrength  = 0.8f;
                settingsData_->turbulenceFrequency = 2.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("嵐##tbPre2")) {
                settingsData_->turbulenceStrength  = 4.0f;
                settingsData_->turbulenceFrequency = 6.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("細かい揺れ##tbPre3")) {
                settingsData_->turbulenceStrength  = 1.5f;
                settingsData_->turbulenceFrequency = 10.0f;
            }
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- 音声振動 ----
    if (settingsData_->enableAudioVibration &&
        effectHeader("音声振動（音の立ち上がりでバンっと揺らす）", ImVec4(0.35f, 0.75f, 0.9f, 1.0f),
                     [&] { settingsData_->enableAudioVibration = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.85f, 1.0f, 1.0f));

        ImGui::TextDisabled("音が大きくなった“瞬間”にバンっと強く震え、その後スッと落ち着きます（各粒子バラバラ／形状を選びません）");

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.28f, 0.38f, 0.5f));
        ImGui::DragFloat("感度##avsens", &settingsData_->audioVibrationSensitivity, 0.05f, 0.0f, 50.0f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("音の立ち上がりへの反応の強さ（入力ゲイン）。大きいほど小さなビートにも反応\n推奨: 2〜10");
        ImGui::DragFloat("振動の大きさ##avs", &settingsData_->audioVibrationStrength, 0.1f, 0.0f, 200.0f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("揺れ幅。大きいほど激しく振動\n推奨: 6〜40");
        ImGui::DragFloat("振動の速さ##avfreq", &settingsData_->audioVibrationFrequency, 0.2f, 0.0f, 120.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("震える速さ（Hz的スケール）。大きいほど細かくブルブル震える\n推奨: 12〜40");
        ImGui::DragFloat("反応カーブ##avsharp", &settingsData_->audioAttackSharpness, 0.02f, 0.1f, 8.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("反応の鋭さ（指数）。1より大きいほど「大きい音だけドンと・小さい音は無視」\n推奨: 1.5〜3");
        ImGui::DragFloat("落ち着く速さ##avrel", &settingsData_->audioReleaseRate, 0.1f, 0.5f, 60.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("バンっの後どれだけ早く静まるか[1/s]。大きいほど一瞬で落ち着く（キレが増す）\n推奨: 6〜20");
        ImGui::PopStyleColor();

        // エンベロープを可視化（CB 注入値をそのまま表示。ビートで跳ねて減衰すれば駆動できている）
        ImGui::Spacing();
        ImGui::TextDisabled("立ち上がり:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.75f, 0.9f, 1.0f));
        ImGui::ProgressBar(settingsData_->audioAmplitude, ImVec2(-1.0f, 0.0f));
        ImGui::PopStyleColor();

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- 終了スケール ----
    if (settingsData_->enableEndScale &&
        effectHeader("終了スケール", ImVec4(0.2f, 0.7f, 0.65f, 1.0f),
                     [&] { settingsData_->enableEndScale = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.4f, 1.0f, 0.9f, 1.0f));
        ImGui::TextDisabled("初期スケール→終了スケールへ寿命に応じてlerp（「寿命で縮小」より優先）");

        bool v = settingsData_->enableEndScale != 0;

        if (v) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.3f, 0.3f, 0.5f));
            ImGui::DragFloat3("終了スケール##esv", &settingsData_->endScaleValue.x, 0.01f, 0.0f, 9999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("寿命終了時のスケール(XYZ)\n0,0,0 で消える / 初期値と同じなら変化なし");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("消える##esPreset1")) {
                settingsData_->endScaleValue = {0.0f, 0.0f, 0.0f};
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("大きくなる##esPreset2")) {
                settingsData_->endScaleValue = {
                    settingsData_->scaleMax * 2.0f,
                    settingsData_->scaleMax * 2.0f,
                    settingsData_->scaleMax * 2.0f};
            }
            ImGui::Unindent();
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- 回転（ランダム初期角度 / ランダム角速度）----
    if ((settingsData_->enableRandomRotation || settingsData_->enableRandomAngularVelocity) &&
        effectHeader("回転", ImVec4(0.75f, 0.3f, 0.75f, 1.0f),
                     [&] { settingsData_->enableRandomRotation = 0; settingsData_->enableRandomAngularVelocity = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.5f, 1.0f, 1.0f));

        // ---- ランダム初期角度 ----
        {
            bool v = settingsData_->enableRandomRotation != 0;
            if (ImGui::Checkbox("ランダム初期角度##rr", &v))
                settingsData_->enableRandomRotation = v ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("発生時にランダムな角度で出現します (XYZ, ラジアン)");
            if (v) {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.15f, 0.35f, 0.5f));

                auto toDeg3 = [](Vector3 r) -> Vector3 { return {r.x * (180.0f / 3.14159265f), r.y * (180.0f / 3.14159265f), r.z * (180.0f / 3.14159265f)}; };
                auto toRad3 = [](Vector3 d) -> Vector3 { return {d.x * (3.14159265f / 180.0f), d.y * (3.14159265f / 180.0f), d.z * (3.14159265f / 180.0f)}; };

                Vector3 rotMinDeg = toDeg3(settingsData_->rotationMin);
                Vector3 rotMaxDeg = toDeg3(settingsData_->rotationMax);

                if (ImGui::DragFloat3("角度 Min(°)##rrMin", &rotMinDeg.x, 1.0f, -360.0f, 360.0f, "%.1f°"))
                    settingsData_->rotationMin = toRad3(rotMinDeg);
                if (ImGui::DragFloat3("角度 Max(°)##rrMax", &rotMaxDeg.x, 1.0f, -360.0f, 360.0f, "%.1f°"))
                    settingsData_->rotationMax = toRad3(rotMaxDeg);

                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::TextDisabled("プリセット:");
                ImGui::SameLine();
                if (ImGui::SmallButton("全方向##rrPreset1")) {
                    settingsData_->rotationMin = {0.0f, 0.0f, 0.0f};
                    settingsData_->rotationMax = {6.2831853f, 6.2831853f, 6.2831853f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Z軸のみ##rrPreset2")) {
                    settingsData_->rotationMin = {0.0f, 0.0f, 0.0f};
                    settingsData_->rotationMax = {0.0f, 0.0f, 6.2831853f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("リセット##rrPreset3")) {
                    settingsData_->rotationMin = {0.0f, 0.0f, 0.0f};
                    settingsData_->rotationMax = {0.0f, 0.0f, 0.0f};
                }
                ImGui::Unindent();
            }
        }

        ImGui::Spacing();

        // ---- ランダム角速度 ----
        {
            bool v = settingsData_->enableRandomAngularVelocity != 0;
            if (ImGui::Checkbox("ランダム角速度##rav", &v))
                settingsData_->enableRandomAngularVelocity = v ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("発生時にランダムな回転速度を設定します (XYZ, ラジアン/秒)");
            if (v) {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.15f, 0.35f, 0.5f));

                auto toDeg3 = [](Vector3 r) -> Vector3 { return {r.x * (180.0f / 3.14159265f), r.y * (180.0f / 3.14159265f), r.z * (180.0f / 3.14159265f)}; };
                auto toRad3 = [](Vector3 d) -> Vector3 { return {d.x * (3.14159265f / 180.0f), d.y * (3.14159265f / 180.0f), d.z * (3.14159265f / 180.0f)}; };

                Vector3 avMinDeg = toDeg3(settingsData_->angularVelocityMin);
                Vector3 avMaxDeg = toDeg3(settingsData_->angularVelocityMax);

                if (ImGui::DragFloat3("角速度 Min(°/s)##ravMin", &avMinDeg.x, 1.0f, -3600.0f, 3600.0f, "%.1f°/s"))
                    settingsData_->angularVelocityMin = toRad3(avMinDeg);
                if (ImGui::DragFloat3("角速度 Max(°/s)##ravMax", &avMaxDeg.x, 1.0f, -3600.0f, 3600.0f, "%.1f°/s"))
                    settingsData_->angularVelocityMax = toRad3(avMaxDeg);

                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::TextDisabled("プリセット:");
                ImGui::SameLine();
                if (ImGui::SmallButton("ゆっくり##ravPreset1")) {
                    settingsData_->angularVelocityMin = {-1.0f, -1.0f, -1.0f};
                    settingsData_->angularVelocityMax = {1.0f, 1.0f, 1.0f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("速い##ravPreset2")) {
                    settingsData_->angularVelocityMin = {-6.2831853f, -6.2831853f, -6.2831853f};
                    settingsData_->angularVelocityMax = {6.2831853f, 6.2831853f, 6.2831853f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Z軸のみ##ravPreset3")) {
                    settingsData_->angularVelocityMin = {0.0f, 0.0f, -3.14159265f};
                    settingsData_->angularVelocityMax = {0.0f, 0.0f, 3.14159265f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("リセット##ravPreset4")) {
                    settingsData_->angularVelocityMin = {0.0f, 0.0f, 0.0f};
                    settingsData_->angularVelocityMax = {0.0f, 0.0f, 0.0f};
                }
                ImGui::Unindent();
            }
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- 放射状速度 ----
    if (settingsData_->enableRadialVelocity &&
        effectHeader("放射状速度", ImVec4(0.85f, 0.5f, 0.1f, 1.0f),
                     [&] { settingsData_->enableRadialVelocity = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
        ImGui::TextDisabled("中心点から放射状に飛び散る速度（花火・爆発の演出に）");

        bool v = settingsData_->enableRadialVelocity != 0;

        if (v) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4f, 0.25f, 0.05f, 0.5f));
            ImGui::DragFloat("放射強度##rs", &settingsData_->radialVelocityStrength, 0.1f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat("ランダム性##rr", &settingsData_->radialVelocityRandomness, 0.01f, 0.0f, 1.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0=完全放射状 / 1=完全ランダム\n推奨: 0.1-0.3");
            ImGui::DragFloat3("放射中心##rc", &settingsData_->radialVelocityCenter.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("花火")) {
                settingsData_->enableRadialVelocity = 1;
                settingsData_->radialVelocityStrength = 5.0f;
                settingsData_->radialVelocityRandomness = 0.2f;
                settingsData_->enableGravity = 1;
                settingsData_->gravity = {0, -9.8f, 0};
                settingsData_->enableVelocityDamping = 1;
                settingsData_->velocityDampingFactor = 0.95f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("爆発")) {
                settingsData_->enableRadialVelocity = 1;
                settingsData_->radialVelocityStrength = 8.0f;
                settingsData_->radialVelocityRandomness = 0.3f;
                settingsData_->enableGravity = 1;
                settingsData_->gravity = {0, -9.8f, 0};
                settingsData_->enableLifetimeVelocityDamping = 1;
                settingsData_->lifetimeVelocityDampingStart = 0.7f;
            }
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // =======================================================
    // 発生形状（ピンク系）【コア・常設】
    // =======================================================
    PushSectionColor(ImVec4(0.85f, 0.35f, 0.6f, 1.0f));
    bool openEmitShape = ImGui::CollapsingHeader("  発生形状");
    PopSectionColor();
    if (openEmitShape) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.6f, 0.8f, 1.0f));

        const char *shapeNames[] = {"Box（直方体）", "Sphere Surface（球面）", "Cone（コーン）"};
        int shape = static_cast<int>(settingsData_->emitShape);
        if (ImGui::Combo("形状##es", &shape, shapeNames, IM_ARRAYSIZE(shapeNames)))
            settingsData_->emitShape = static_cast<uint32_t>(shape);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "モデル/プリミティブ付きエミッターには適用されません（モデルなしのみ有効）\n"
                "Box: エミッターのScaleが発生ボックスの半辺になります\n"
                "Sphere/Cone: 下の半径パラメータで範囲を指定");

        if (settingsData_->emitShape == 0) {
            ImGui::TextDisabled("  ← エミッターのScale（変換設定）で発生範囲を調整");
        }

        if (settingsData_->emitShape == 1 || settingsData_->emitShape == 2) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4f, 0.15f, 0.25f, 0.5f));
            ImGui::DragFloat("半径##esr", &settingsData_->emitSphereRadius, 0.05f, 0.0f, 999.0f, "%.4f");
            if (settingsData_->emitShape == 2) {
                float angleDeg = settingsData_->emitConeAngle * (180.0f / 3.14159265f);
                if (ImGui::DragFloat("半開角(°)##eca", &angleDeg, 1.0f, 1.0f, 180.0f, "%.1f°"))
                    settingsData_->emitConeAngle = angleDeg * (3.14159265f / 180.0f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("コーンの広がり角度（片側）\n30°=細め / 90°=半球");
            }
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (settingsData_->emitShape == 1) {
                if (ImGui::SmallButton("小球##espre1")) settingsData_->emitSphereRadius = 0.5f;
                ImGui::SameLine();
                if (ImGui::SmallButton("爆発球##espre2")) settingsData_->emitSphereRadius = 2.0f;
            } else {
                if (ImGui::SmallButton("細コーン##ecpre1")) {
                    settingsData_->emitSphereRadius = 3.0f;
                    settingsData_->emitConeAngle    = 0.2618f; // 15°
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("広コーン##ecpre2")) {
                    settingsData_->emitSphereRadius = 2.0f;
                    settingsData_->emitConeAngle    = 0.7854f; // 45°
                }
            }
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- ギャザー（集合）----
    if (settingsData_->enableGather &&
        effectHeader("ギャザー（集合）", ImVec4(0.6f, 0.2f, 0.8f, 1.0f),
                     [&] { settingsData_->enableGather = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.8f, 0.5f, 1.0f, 1.0f));

        bool v = settingsData_->enableGather != 0;

        if (v) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.1f, 0.4f, 0.5f));
            ImGui::DragFloat("開始タイミング##gs", &settingsData_->gatherStartRatio, 0.01f, 0.0f, 1.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("寿命の何%%から引き寄せを開始するか");
            ImGui::DragFloat("ギャザー強度##gstr", &settingsData_->gatherStrength, 0.1f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat3("目標座標##gt", &settingsData_->gatherTargetOffset.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();
            bool gft = settingsData_->enableGatherForTrail != 0;
            if (ImGui::Checkbox("トレイルにも適用##gft", &gft))
                settingsData_->enableGatherForTrail = gft ? 1 : 0;
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- 渦巻き（Vortex）----
    if (settingsData_->enableVortex &&
        effectHeader("渦巻き（Vortex）", ImVec4(0.1f, 0.65f, 0.75f, 1.0f),
                     [&] { settingsData_->enableVortex = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.3f, 0.9f, 1.0f, 1.0f));

        bool v = settingsData_->enableVortex != 0;

        if (v) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.3f, 0.4f, 0.5f));
            ImGui::DragFloat("回転強度##vstr", &settingsData_->vortexStrength, 0.1f, -999.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("+ で正回転 / - で逆回転");
            ImGui::DragFloat3("目標座標##vt", &settingsData_->vortexTargetOffset.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();

            ImGui::Text("回転軸:");
            ImGui::SameLine();
            if (ImGui::SmallButton("X##vx"))
                settingsData_->vortexAxis = {1, 0, 0};
            ImGui::SameLine();
            if (ImGui::SmallButton("Y##vy"))
                settingsData_->vortexAxis = {0, 1, 0};
            ImGui::SameLine();
            if (ImGui::SmallButton("Z##vz"))
                settingsData_->vortexAxis = {0, 0, 1};
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.3f, 0.4f, 0.5f));
            ImGui::DragFloat3("軸ベクトル##vax", &settingsData_->vortexAxis.x, 0.05f, -1.0f, 1.0f, "%.4f");
            ImGui::PopStyleColor();

            bool vft = settingsData_->enableVortexForTrail != 0;
            if (ImGui::Checkbox("トレイルにも適用##vft", &vft))
                settingsData_->enableVortexForTrail = vft ? 1 : 0;
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- カールノイズ ----
    if (settingsData_->enableCurlNoise &&
        effectHeader("カールノイズ", ImVec4(0.0f, 0.8f, 0.7f, 1.0f),
                     [&] { settingsData_->enableCurlNoise = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 1.0f, 0.9f, 1.0f));

        bool v = settingsData_->enableCurlNoise != 0;

        if (v) {
            // --------------------------------------------------
            // ブレンドモード
            // --------------------------------------------------
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("速度合成モード");
            ImGui::PopStyleColor();
            ImGui::Separator();

            {
                int blendMode = static_cast<int>(settingsData_->curlNoiseBlendMode);

                // ラジオボタン：置き換え
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 1.0f, 0.9f, 1.0f));
                if (ImGui::RadioButton("置き換え##cnbm0", blendMode == 0))
                    settingsData_->curlNoiseBlendMode = 0;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("velocity を完全に置き換える（従来動作）\n流体的な挙動。Gather/Vortex の影響は受けない。");

                ImGui::SameLine();

                // ラジオボタン：加算
                if (ImGui::RadioButton("加算##cnbm1", blendMode == 1))
                    settingsData_->curlNoiseBlendMode = 1;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("既存の velocity に加算する\nGather / Vortex 等と組み合わせて使える。\n加速しすぎる場合は強度を下げるか速度減衰を併用。");
                ImGui::PopStyleColor();

                // 加算モード時の注意表示
                if (blendMode == 1) {
                    ImGui::Indent();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
                    ImGui::TextUnformatted("! 速度減衰との併用を推奨");
                    ImGui::PopStyleColor();
                    ImGui::Unindent();
                }
            }

            ImGui::Spacing();

            // --------------------------------------------------
            // ノイズパラメータ
            // --------------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("ノイズパラメータ");
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.35f, 0.3f, 0.5f));
            ImGui::DragFloat("スケール##cns", &settingsData_->curlNoiseScale, 0.01f, 0.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("小 → 大きくゆったりした渦\n大 → 細かい乱流");
            ImGui::DragFloat("強度##cnstr", &settingsData_->curlNoiseStrength, 0.1f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat("時間変化##cntm", &settingsData_->curlNoiseTimeScale, 0.01f, 0.0f, 99.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0.0 = 固定フロー / 大 = 激しく変化");
            ImGui::PopStyleColor();

            int oct = static_cast<int>(settingsData_->curlNoiseOctaves);
            if (ImGui::DragInt("オクターブ##cno", &oct, 1, 1, 16))
                settingsData_->curlNoiseOctaves = static_cast<uint32_t>(oct);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("1=軽量なめらか / 4=複雑（負荷増）");

            // --------------------------------------------------
            // 分散オフセット
            // --------------------------------------------------
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("分散オフセット");
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.35f, 0.3f, 0.5f));
            ImGui::DragFloat("分散強度##cnprs", &settingsData_->curlNoisePosRandomStrength, 0.05f, 0.0f, 10.0f, "%.4f");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "エミッタが小さく全員が同じ位置から生まれるとき、\n"
                    "全パーティクルが同一方向に動くのを防ぐ。\n"
                    "各パーティクル固有のオフセットをサンプリング座標に加算し\n"
                    "異なるノイズフィールドを参照させる。\n\n"
                    "0.0 = オフセットなし（従来動作）\n"
                    "推奨: 0.5〜2.0  一点から広がる演出に");

            // 分散強度が有効なとき視覚的な補足を表示
            if (settingsData_->curlNoisePosRandomStrength > 0.0f) {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 0.8f));
                ImGui::Text("  現在: %.4f  (各パーティクルが固有の方向に発散)", settingsData_->curlNoisePosRandomStrength);
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }

            // --------------------------------------------------
            // 引き戻し設定
            // --------------------------------------------------
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("引き戻し設定");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.35f, 0.3f, 0.5f));
            ImGui::DragFloat("引き戻し強度##cna", &settingsData_->curlNoiseAttractStrength, 0.01f, 0.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = 無効\n大きいほどエミッター付近に密集して流れる");
            ImGui::DragFloat3("引き戻しオフセット##cnac", &settingsData_->curlNoiseAttractCenter.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "エミッター座標を基準としたオフセット。\n"
                    "(0, 0, 0) でエミッターの中心に引き戻す。\n"
                    "C++側で毎フレーム\n"
                    "  settingsData_->curlNoiseAttractCenter =\n"
                    "      emitterPos + offset;\n"
                    "として渡すこと。");
            ImGui::PopStyleColor();

            // --------------------------------------------------
            // プリセット
            // --------------------------------------------------
            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("煙・霧")) {
                settingsData_->curlNoiseScale = 0.4f;
                settingsData_->curlNoiseStrength = 1.5f;
                settingsData_->curlNoiseTimeScale = 0.15f;
                settingsData_->curlNoiseOctaves = 2;
                settingsData_->curlNoiseAttractStrength = 0.3f;
                settingsData_->curlNoiseBlendMode = 0;
                settingsData_->curlNoisePosRandomStrength = 0.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("炎・乱流")) {
                settingsData_->curlNoiseScale = 1.2f;
                settingsData_->curlNoiseStrength = 4.0f;
                settingsData_->curlNoiseTimeScale = 0.6f;
                settingsData_->curlNoiseOctaves = 3;
                settingsData_->curlNoiseAttractStrength = 0.8f;
                settingsData_->curlNoiseBlendMode = 0;
                settingsData_->curlNoisePosRandomStrength = 0.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("水流")) {
                settingsData_->curlNoiseScale = 0.7f;
                settingsData_->curlNoiseStrength = 2.5f;
                settingsData_->curlNoiseTimeScale = 0.25f;
                settingsData_->curlNoiseOctaves = 2;
                settingsData_->curlNoiseAttractStrength = 0.5f;
                settingsData_->curlNoiseBlendMode = 0;
                settingsData_->curlNoisePosRandomStrength = 0.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("一点放射")) {
                // 小さいエミッタから四方八方に広がる演出向けプリセット
                settingsData_->curlNoiseScale = 0.6f;
                settingsData_->curlNoiseStrength = 2.0f;
                settingsData_->curlNoiseTimeScale = 0.2f;
                settingsData_->curlNoiseOctaves = 2;
                settingsData_->curlNoiseAttractStrength = 0.0f;
                settingsData_->curlNoiseBlendMode = 0;
                settingsData_->curlNoisePosRandomStrength = 1.5f;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("小さいエミッタから四方八方に広がる演出向け\n分散強度: 1.5 を設定します");
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- トレイル ----
    if (settingsData_->enableTrail &&
        effectHeader("トレイル", ImVec4(0.2f, 0.7f, 0.35f, 1.0f),
                     [&] { settingsData_->enableTrail = 0; })) {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));

        bool v = settingsData_->enableTrail != 0;

        if (v) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.3f, 0.15f, 0.5f));

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("基本設定");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::DragFloat("生成間隔距離##tsd", &settingsData_->trailSpawnDistance, 0.01f, 0.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("この距離ごとにトレイルを生成\n小さいほど滑らか（推奨: 0.05-0.15）");
            int maxT = static_cast<int>(settingsData_->maxTrailPerParticle);
            if (ImGui::DragInt("最大数/親##tmax", &maxT, 1, 1, 1000))
                settingsData_->maxTrailPerParticle = static_cast<uint32_t>(maxT);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("トレイル特性");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::DragFloat("寿命倍率##tlt", &settingsData_->trailLifeTimeScale, 0.05f, 0.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("親の残り寿命に対する倍率（推奨: 0.8-1.5）");
            ImGui::DragFloat("最小寿命(s)##tmn", &settingsData_->trailMinLifeTime, 0.05f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat3("スケール倍率##tsc", &settingsData_->trailScaleMultiplier.x, 0.01f, 0.0f, 999.0f, "%.4f");
            ImGui::ColorEdit4("色倍率##tco", &settingsData_->trailColorMultiplier.x);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("速度設定");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::PopStyleColor(); // FrameBg

            bool inh = settingsData_->trailInheritVelocity != 0;
            if (ImGui::Checkbox("親の速度を継承##tiv", &inh))
                settingsData_->trailInheritVelocity = inh ? 1 : 0;
            if (inh) {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.3f, 0.15f, 0.5f));
                ImGui::DragFloat("速度倍率##tvs", &settingsData_->trailVelocityScale, 0.01f, 0.0f, 99.0f, "%.4f");
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // =======================================================
    // 10. Debug Info（常時表示）
    // =======================================================
    static const int kHistorySize = 256;

    ImGui::Spacing();
    ImGui::Separator();
    {
        int32_t headVal = 0, tailVal = 0;
        int32_t *p = nullptr;
        D3D12_RANGE r = {0, sizeof(int32_t)};
        if (SUCCEEDED(freeListIndexReadbackBuffer_->Map(0, &r, reinterpret_cast<void **>(&p)))) {
            headVal = *p;
            freeListIndexReadbackBuffer_->Unmap(0, nullptr);
        }
        if (SUCCEEDED(freeListTrailIndexReadbackBuffer_->Map(0, &r, reinterpret_cast<void **>(&p)))) {
            tailVal = *p;
            freeListTrailIndexReadbackBuffer_->Unmap(0, nullptr);
        }
        int32_t used = settingsData_->maxParticleCount - (tailVal - headVal);
        float rate = (float)used / (float)settingsData_->maxParticleCount;

        static float particleHistory[kHistorySize] = {};
        static float particleRateHistory[kHistorySize] = {};
        static int histOffset = 0;
        particleHistory[histOffset] = (float)used;
        particleRateHistory[histOffset] = rate * 100.0f;
        histOffset = (histOffset + 1) % kHistorySize;

        char overlay[64];
        sprintf_s(overlay, "%d / %d  (%.1f%%)", used, (int)settingsData_->maxParticleCount, rate * 100.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              rate >= 0.9f   ? ImVec4(1.0f, 0.3f, 0.3f, 1)
                              : rate >= 0.7f ? ImVec4(1.0f, 0.9f, 0.2f, 1)
                                             : ImVec4(0.4f, 1.0f, 0.4f, 1));
        ImGui::ProgressBar(rate, ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
        ImGui::TextUnformatted("パーティクル数 (履歴)");
        ImGui::PopStyleColor();

        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.05f, 0.05f, 0.09f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));

        if (ImPlot::BeginPlot("##ParticleCount", ImVec2(-1, 80),
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoInputs |
                                  ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
            ImPlot::SetupAxes(nullptr, nullptr,
                              ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines,
                              ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHistorySize, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, (double)settingsData_->maxParticleCount, ImPlotCond_Always);
            ImPlot::PlotLine("##pc", particleHistory, kHistorySize, 1.0, 0.0,
                             ImPlotLineFlags_None, histOffset);
            ImPlot::EndPlot();
        }

        ImPlot::PopStyleColor(3);
    }

    ImGui::PopItemWidth();

    if (settingsData_->enableGather)
        DrawLine3D::GetInstance()->DrawSphere(settingsData_->gatherTarget, {1.0f, 0.0f, 1.0f, 1.0f}, 0.1f, 8);
    if (settingsData_->enableVortex)
        DrawLine3D::GetInstance()->DrawSphere(settingsData_->vortexTarget, {0.5f, 1.0f, 0.0f, 1.0f}, 0.1f, 8);

#endif // USE_IMGUI
}
} // namespace Hagine
