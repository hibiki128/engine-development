#define NOMINMAX
#include "ParticleCSFieldManager.h"
#include "Utility/Debug/ImGui/ImGuiNotification.h"
#include <cassert>
#include <cmath>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

#pragma pack(push, 1)
namespace Hagine {
struct GPU_FieldSettingsOverride {
    uint32_t overrideMaskX; // overrideMask.x (bit0-31)
    uint32_t overrideMaskY; // overrideMask.y (bit32-63)
    float maskPadding[2];   // アライメント
    float lifeTimeMin;
    float lifeTimeMax;
    float scaleMin;
    float scaleMax;
    float velocityMin[3];
    float pad1;
    float velocityMax[3];
    float pad2;
    float startColor[4];
    float endColor[4];
    uint32_t enableLifetimeScale;
    uint32_t enableRandomColor;
    uint32_t enableSinScale;
    float sinScaleFrequency;
    float sinScaleAmplitude;
    uint32_t enableGravity;
    float pad3[2];
    float gravity[3];
    float pad4;
    uint32_t enableTrail;
    float trailSpawnDistance;
    uint32_t maxTrailPerParticle;
    float trailLifeTimeScale;
    float trailScaleMultiplier[3];
    float pad5;
    float trailColorMultiplier[4];
    float trailVelocityScale;
    uint32_t trailInheritVelocity;
    float trailMinLifeTime;
    float pad6;
    uint32_t enableGather;
    float gatherStartRatio;
    float gatherStrength;
    float pad7;
    float gatherTarget[3];
    float pad8;
    uint32_t enableVortex;
    float vortexStrength;
    float pad9[2];
    float vortexAxis[3];
    float pad10;
    uint32_t enableAcceleration;
    float pad11[3];
    float acceleration[3];
    float pad12;
    uint32_t enableVelocityDamping;
    float velocityDampingFactor;
    uint32_t enableLifetimeVelDamping;
    float lifetimeVelDampingStart;
    uint32_t enableCurlNoise;
    float curlNoiseScale;
    float curlNoiseStrength;
    float curlNoiseTimeScale;
    uint32_t curlNoiseOctaves;
    float curlNoiseAttractStrength;
    uint32_t curlNoiseBlendMode;
    float curlNoisePosRandom;
};
#pragma pack(pop)

static constexpr size_t kGPUOverrideStride = sizeof(GPU_FieldSettingsOverride);

static void PackOverrideToGPU(const ParticleFieldSettingsOverride &src, GPU_FieldSettingsOverride &dst) {
    dst.overrideMaskX = static_cast<uint32_t>(src.overrideMask & 0xFFFFFFFFULL);
    dst.overrideMaskY = static_cast<uint32_t>((src.overrideMask >> 32) & 0xFFFFFFFFULL);
    dst.maskPadding[0] = dst.maskPadding[1] = 0.0f;
    dst.lifeTimeMin = src.lifeTimeMin;
    dst.lifeTimeMax = src.lifeTimeMax;
    dst.scaleMin = src.scaleMin;
    dst.scaleMax = src.scaleMax;
    dst.velocityMin[0] = src.velocityMin.x;
    dst.velocityMin[1] = src.velocityMin.y;
    dst.velocityMin[2] = src.velocityMin.z;
    dst.pad1 = 0.0f;
    dst.velocityMax[0] = src.velocityMax.x;
    dst.velocityMax[1] = src.velocityMax.y;
    dst.velocityMax[2] = src.velocityMax.z;
    dst.pad2 = 0.0f;
    dst.startColor[0] = src.startColor.x;
    dst.startColor[1] = src.startColor.y;
    dst.startColor[2] = src.startColor.z;
    dst.startColor[3] = src.startColor.w;
    dst.endColor[0] = src.endColor.x;
    dst.endColor[1] = src.endColor.y;
    dst.endColor[2] = src.endColor.z;
    dst.endColor[3] = src.endColor.w;
    dst.enableLifetimeScale = src.enableLifetimeScale;
    dst.enableRandomColor = src.enableRandomColor;
    dst.enableSinScale = src.enableSinScale;
    dst.sinScaleFrequency = src.sinScaleFrequency;
    dst.sinScaleAmplitude = src.sinScaleAmplitude;
    dst.enableGravity = src.enableGravity;
    dst.pad3[0] = dst.pad3[1] = 0.0f;
    dst.gravity[0] = src.gravity.x;
    dst.gravity[1] = src.gravity.y;
    dst.gravity[2] = src.gravity.z;
    dst.pad4 = 0.0f;
    dst.enableTrail = src.enableTrail;
    dst.trailSpawnDistance = src.trailSpawnDistance;
    dst.maxTrailPerParticle = src.maxTrailPerParticle;
    dst.trailLifeTimeScale = src.trailLifeTimeScale;
    dst.trailScaleMultiplier[0] = src.trailScaleMultiplier.x;
    dst.trailScaleMultiplier[1] = src.trailScaleMultiplier.y;
    dst.trailScaleMultiplier[2] = src.trailScaleMultiplier.z;
    dst.pad5 = 0.0f;
    dst.trailColorMultiplier[0] = src.trailColorMultiplier.x;
    dst.trailColorMultiplier[1] = src.trailColorMultiplier.y;
    dst.trailColorMultiplier[2] = src.trailColorMultiplier.z;
    dst.trailColorMultiplier[3] = src.trailColorMultiplier.w;
    dst.trailVelocityScale = src.trailVelocityScale;
    dst.trailInheritVelocity = src.trailInheritVelocity;
    dst.trailMinLifeTime = src.trailMinLifeTime;
    dst.pad6 = 0.0f;
    dst.enableGather = src.enableGather;
    dst.gatherStartRatio = src.gatherStartRatio;
    dst.gatherStrength = src.gatherStrength;
    dst.pad7 = 0.0f;
    dst.gatherTarget[0] = src.gatherTarget.x;
    dst.gatherTarget[1] = src.gatherTarget.y;
    dst.gatherTarget[2] = src.gatherTarget.z;
    dst.pad8 = 0.0f;
    dst.enableVortex = src.enableVortex;
    dst.vortexStrength = src.vortexStrength;
    dst.pad9[0] = dst.pad9[1] = 0.0f;
    dst.vortexAxis[0] = src.vortexAxis.x;
    dst.vortexAxis[1] = src.vortexAxis.y;
    dst.vortexAxis[2] = src.vortexAxis.z;
    dst.pad10 = 0.0f;
    dst.enableAcceleration = src.enableAcceleration;
    dst.pad11[0] = dst.pad11[1] = dst.pad11[2] = 0.0f;
    dst.acceleration[0] = src.acceleration.x;
    dst.acceleration[1] = src.acceleration.y;
    dst.acceleration[2] = src.acceleration.z;
    dst.pad12 = 0.0f;
    dst.enableVelocityDamping = src.enableVelocityDamping;
    dst.velocityDampingFactor = src.velocityDampingFactor;
    dst.enableLifetimeVelDamping = src.enableLifetimeVelDamping;
    dst.lifetimeVelDampingStart = src.lifetimeVelDampingStart;
    dst.enableCurlNoise = src.enableCurlNoise;
    dst.curlNoiseScale = src.curlNoiseScale;
    dst.curlNoiseStrength = src.curlNoiseStrength;
    dst.curlNoiseTimeScale = src.curlNoiseTimeScale;
    dst.curlNoiseOctaves = src.curlNoiseOctaves;
    dst.curlNoiseAttractStrength = src.curlNoiseAttractStrength;
    dst.curlNoiseBlendMode = src.curlNoiseBlendMode;
    dst.curlNoisePosRandom = src.curlNoisePosRandom;
}


void ParticleCSFieldManager::Finalize() {
    // マップ中のリソースはアンマップしてから解放する
    if (fieldsResource_) {
        fieldsResource_->Unmap(0, nullptr);
        fieldsMappedData_ = nullptr;
    }
    if (fieldCountResource_) {
        fieldCountResource_->Unmap(0, nullptr);
        fieldCountMappedData_ = nullptr;
    }
    if (overrideResource_) {
        overrideResource_->Unmap(0, nullptr);
        overrideMappedData_ = nullptr;
    }

    // ComPtr は Reset() で明示的に解放（デストラクタでも自動解放される）
    fieldsResource_.Reset();
    fieldCountResource_.Reset();
    zeroFieldCountResource_.Reset();
    overrideResource_.Reset();

    fields_.clear();
}


void ParticleCSFieldManager::Initialize() {
    dxCommon_ = ParticleCommon::GetInstance()->GetDxCommon();
    srvManager_ = SrvManager::GetInstance();
    CreateGPUResources();
}

void ParticleCSFieldManager::CreateGPUResources() {
    // フィールド配列バッファ（StructuredBuffer として使う）
    size_t bufSize = sizeof(ParticleFieldData) * kMaxFields;
    fieldsResource_ = dxCommon_->CreateBufferResource(bufSize);
    fieldsResource_->Map(0, nullptr, reinterpret_cast<void **>(&fieldsMappedData_));
    ZeroMemory(fieldsMappedData_, bufSize);

    // フィールド数バッファ（ConstantBuffer）
    fieldCountResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * 4); // アライメント
    fieldCountResource_->Map(0, nullptr, reinterpret_cast<void **>(&fieldCountMappedData_));
    *fieldCountMappedData_ = 0;

    zeroFieldCountResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * 4);
    uint32_t *zeroPtr = nullptr;
    zeroFieldCountResource_->Map(0, nullptr, reinterpret_cast<void **>(&zeroPtr));
    *zeroPtr = 0;
    zeroFieldCountResource_->Unmap(0, nullptr);

    // SRV 登録 (fields)
    fieldsSrvIndex_ = srvManager_->Allocate() + 1;
    fieldsSrvHandle_.first = srvManager_->GetCPUDescriptorHandle(fieldsSrvIndex_);
    fieldsSrvHandle_.second = srvManager_->GetGPUDescriptorHandle(fieldsSrvIndex_);
    srvManager_->CreateSRVforStructuredBuffer(
        fieldsSrvIndex_,
        fieldsResource_.Get(),
        kMaxFields,
        sizeof(ParticleFieldData));

    // 設定上書きバッファ（StructuredBuffer: gFieldsOverride t1）
    // HLSL の ParticleFieldSettingsOverrideData と同じレイアウトを
    // GPU_FieldSettingsOverride として扱う（サイズだけ合わせる）
    size_t overrideBufSize = kGPUOverrideStride * kMaxFields;
    overrideResource_ = dxCommon_->CreateBufferResource(overrideBufSize);
    overrideResource_->Map(0, nullptr, &overrideMappedData_);
    ZeroMemory(overrideMappedData_, overrideBufSize);

    overrideSrvIndex_ = srvManager_->Allocate() + 1;
    overrideSrvHandle_.first = srvManager_->GetCPUDescriptorHandle(overrideSrvIndex_);
    overrideSrvHandle_.second = srvManager_->GetGPUDescriptorHandle(overrideSrvIndex_);
    srvManager_->CreateSRVforStructuredBuffer(
        overrideSrvIndex_,
        overrideResource_.Get(),
        kMaxFields,
        kGPUOverrideStride);
}

void ParticleCSFieldManager::Update() {
    UploadToGPU();
}

void ParticleCSFieldManager::UploadToGPU() {
    uint32_t count = 0;
    for (auto &f : fields_) {
        if (!f.enabled)
            continue;
        if (count >= kMaxFields)
            break;
        fieldsMappedData_[count] = f.data;

        // 設定上書きデータを GPU レイアウト構造体へパック
        auto *dst = reinterpret_cast<GPU_FieldSettingsOverride *>(
            static_cast<uint8_t *>(overrideMappedData_) + count * kGPUOverrideStride);
        PackOverrideToGPU(f.override_, *dst);

        count++;
    }
    *fieldCountMappedData_ = count;
}

void ParticleCSFieldManager::AddField(const ParticleField &field) {
    if (static_cast<uint32_t>(fields_.size()) >= kMaxFields)
        return;
    fields_.push_back(field);
}

void ParticleCSFieldManager::RemoveField(int index) {
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        return;
    fields_.erase(fields_.begin() + index);
}

ParticleField *ParticleCSFieldManager::GetField(int index) {
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        return nullptr;
    return &fields_[index];
}

// =============================================
// セーブ / ロード
// =============================================

void ParticleCSFieldManager::SaveFieldData(DataHandler &data, const ParticleField &field) {
    // 基本情報
    data.Save("name", field.name);
    data.Save("enabled", field.enabled);

    // フィールドデータ
    data.Save("fieldType", field.data.fieldType);
    data.Save<Vector3>("position", field.data.position);
    data.Save("radius", field.data.radius);
    data.Save<Vector3>("direction", field.data.direction);
    data.Save("strength", field.data.strength);
    data.Save("falloff", field.data.falloff);

    // 寿命ドレイン
    data.Save("enableLifeDrain", field.data.enableLifeDrain);
    data.Save("lifeTimeDrain", field.data.lifeTimeDrain);

    // トレイル強制生成
    data.Save("enableForceTrail", field.data.enableForceTrail);
    data.Save("trailSpawnDistanceOverride", field.data.trailSpawnDistanceOverride);

    // カラー乗算
    data.Save("enableColorMultiply", field.data.enableColorMultiply);
    data.Save<Vector4>("colorMultiplier", field.data.colorMultiplier);

    // 一度きり設定上書き
    data.Save("enableSettingsOverride", field.data.enableSettingsOverride);
    if (field.data.enableSettingsOverride) {
        SaveOverrideData(data, field.override_);
    }

    // Emit時スポーン判定
    data.Save("enableEmitSpawn", field.data.enableEmitSpawn);
    if (field.data.enableEmitSpawn) {
        data.Save("emitSpawnLifeTimeMin", field.data.emitSpawnLifeTimeMin);
        data.Save("emitSpawnLifeTimeMax", field.data.emitSpawnLifeTimeMax);
        data.Save("emitSpawnCount", field.data.emitSpawnCount);
    }

    // グループID
    data.Save("groupId", field.data.groupId);
}

void ParticleCSFieldManager::LoadFieldData(DataHandler &data, ParticleField &field) {
    // 基本情報
    field.name = data.Load("name", field.name);
    field.enabled = data.Load("enabled", field.enabled);

    // フィールドデータ
    field.data.fieldType = data.Load("fieldType", field.data.fieldType);
    field.data.position = data.Load<Vector3>("position", field.data.position);
    field.data.radius = data.Load("radius", field.data.radius);
    field.data.direction = data.Load<Vector3>("direction", field.data.direction);
    field.data.strength = data.Load("strength", field.data.strength);
    field.data.falloff = data.Load("falloff", field.data.falloff);

    // 寿命ドレイン
    field.data.enableLifeDrain = data.Load("enableLifeDrain", field.data.enableLifeDrain);
    field.data.lifeTimeDrain = data.Load("lifeTimeDrain", field.data.lifeTimeDrain);

    // トレイル強制生成
    field.data.enableForceTrail = data.Load("enableForceTrail", field.data.enableForceTrail);
    field.data.trailSpawnDistanceOverride = data.Load("trailSpawnDistanceOverride", field.data.trailSpawnDistanceOverride);

    // カラー乗算
    field.data.enableColorMultiply = data.Load("enableColorMultiply", field.data.enableColorMultiply);
    field.data.colorMultiplier = data.Load<Vector4>("colorMultiplier", field.data.colorMultiplier);

    // 一度きり設定上書き
    field.data.enableSettingsOverride = data.Load("enableSettingsOverride", field.data.enableSettingsOverride);
    if (field.data.enableSettingsOverride) {
        LoadOverrideData(data, field.override_);
    }

    // Emit時スポーン判定
    field.data.enableEmitSpawn = data.Load("enableEmitSpawn", field.data.enableEmitSpawn);
    if (field.data.enableEmitSpawn) {
        field.data.emitSpawnLifeTimeMin = data.Load("emitSpawnLifeTimeMin", field.data.emitSpawnLifeTimeMin);
        field.data.emitSpawnLifeTimeMax = data.Load("emitSpawnLifeTimeMax", field.data.emitSpawnLifeTimeMax);
        field.data.emitSpawnCount = data.Load("emitSpawnCount", field.data.emitSpawnCount);
    }

    // グループID
    field.data.groupId = data.Load("groupId", field.data.groupId);
}

void ParticleCSFieldManager::SaveOverrideData(DataHandler &data, const ParticleFieldSettingsOverride &ov) {
    // overrideMask を上位/下位 32bit に分けて保存（uint64_t は DataHandler 非対応の場合に対応）
    data.Save("ov_maskLo", static_cast<uint32_t>(ov.overrideMask & 0xFFFFFFFFULL));
    data.Save("ov_maskHi", static_cast<uint32_t>((ov.overrideMask >> 32) & 0xFFFFFFFFULL));
    data.Save("ov_lifeTimeMin", ov.lifeTimeMin);
    data.Save("ov_lifeTimeMax", ov.lifeTimeMax);
    data.Save("ov_scaleMin", ov.scaleMin);
    data.Save("ov_scaleMax", ov.scaleMax);
    data.Save<Vector3>("ov_velocityMin", ov.velocityMin);
    data.Save<Vector3>("ov_velocityMax", ov.velocityMax);
    data.Save<Vector4>("ov_startColor", ov.startColor);
    data.Save<Vector4>("ov_endColor", ov.endColor);
    data.Save("ov_enableLifetimeScale", ov.enableLifetimeScale);
    data.Save("ov_enableRandomColor", ov.enableRandomColor);
    data.Save("ov_enableSinScale", ov.enableSinScale);
    data.Save("ov_sinScaleFrequency", ov.sinScaleFrequency);
    data.Save("ov_sinScaleAmplitude", ov.sinScaleAmplitude);
    data.Save("ov_enableGravity", ov.enableGravity);
    data.Save<Vector3>("ov_gravity", ov.gravity);
    data.Save("ov_enableTrail", ov.enableTrail);
    data.Save("ov_trailSpawnDistance", ov.trailSpawnDistance);
    data.Save("ov_maxTrailPerParticle", ov.maxTrailPerParticle);
    data.Save("ov_trailLifeTimeScale", ov.trailLifeTimeScale);
    data.Save<Vector3>("ov_trailScaleMultiplier", ov.trailScaleMultiplier);
    data.Save<Vector4>("ov_trailColorMultiplier", ov.trailColorMultiplier);
    data.Save("ov_trailVelocityScale", ov.trailVelocityScale);
    data.Save("ov_trailInheritVelocity", ov.trailInheritVelocity);
    data.Save("ov_trailMinLifeTime", ov.trailMinLifeTime);
    data.Save("ov_enableGather", ov.enableGather);
    data.Save("ov_gatherStartRatio", ov.gatherStartRatio);
    data.Save("ov_gatherStrength", ov.gatherStrength);
    data.Save<Vector3>("ov_gatherTarget", ov.gatherTarget);
    data.Save("ov_enableVortex", ov.enableVortex);
    data.Save("ov_vortexStrength", ov.vortexStrength);
    data.Save<Vector3>("ov_vortexAxis", ov.vortexAxis);
    data.Save("ov_enableAcceleration", ov.enableAcceleration);
    data.Save<Vector3>("ov_acceleration", ov.acceleration);
    data.Save("ov_enableVelocityDamping", ov.enableVelocityDamping);
    data.Save("ov_velocityDampingFactor", ov.velocityDampingFactor);
    data.Save("ov_enableLifetimeVelDamping", ov.enableLifetimeVelDamping);
    data.Save("ov_lifetimeVelDampingStart", ov.lifetimeVelDampingStart);
    data.Save("ov_enableCurlNoise", ov.enableCurlNoise);
    data.Save("ov_curlNoiseScale", ov.curlNoiseScale);
    data.Save("ov_curlNoiseStrength", ov.curlNoiseStrength);
    data.Save("ov_curlNoiseTimeScale", ov.curlNoiseTimeScale);
    data.Save("ov_curlNoiseOctaves", ov.curlNoiseOctaves);
    data.Save("ov_curlNoiseAttractStrength", ov.curlNoiseAttractStrength);
    data.Save("ov_curlNoiseBlendMode", ov.curlNoiseBlendMode);
    data.Save("ov_curlNoisePosRandom", ov.curlNoisePosRandom);
}

void ParticleCSFieldManager::LoadOverrideData(DataHandler &data, ParticleFieldSettingsOverride &ov) {
    uint32_t lo = data.Load("ov_maskLo", static_cast<uint32_t>(ov.overrideMask & 0xFFFFFFFFULL));
    uint32_t hi = data.Load("ov_maskHi", static_cast<uint32_t>((ov.overrideMask >> 32) & 0xFFFFFFFFULL));
    ov.overrideMask = (static_cast<uint64_t>(hi) << 32) | lo;
    ov.lifeTimeMin = data.Load("ov_lifeTimeMin", ov.lifeTimeMin);
    ov.lifeTimeMax = data.Load("ov_lifeTimeMax", ov.lifeTimeMax);
    ov.scaleMin = data.Load("ov_scaleMin", ov.scaleMin);
    ov.scaleMax = data.Load("ov_scaleMax", ov.scaleMax);
    ov.velocityMin = data.Load<Vector3>("ov_velocityMin", ov.velocityMin);
    ov.velocityMax = data.Load<Vector3>("ov_velocityMax", ov.velocityMax);
    ov.startColor = data.Load<Vector4>("ov_startColor", ov.startColor);
    ov.endColor = data.Load<Vector4>("ov_endColor", ov.endColor);
    ov.enableLifetimeScale = data.Load("ov_enableLifetimeScale", ov.enableLifetimeScale);
    ov.enableRandomColor = data.Load("ov_enableRandomColor", ov.enableRandomColor);
    ov.enableSinScale = data.Load("ov_enableSinScale", ov.enableSinScale);
    ov.sinScaleFrequency = data.Load("ov_sinScaleFrequency", ov.sinScaleFrequency);
    ov.sinScaleAmplitude = data.Load("ov_sinScaleAmplitude", ov.sinScaleAmplitude);
    ov.enableGravity = data.Load("ov_enableGravity", ov.enableGravity);
    ov.gravity = data.Load<Vector3>("ov_gravity", ov.gravity);
    ov.enableTrail = data.Load("ov_enableTrail", ov.enableTrail);
    ov.trailSpawnDistance = data.Load("ov_trailSpawnDistance", ov.trailSpawnDistance);
    ov.maxTrailPerParticle = data.Load("ov_maxTrailPerParticle", ov.maxTrailPerParticle);
    ov.trailLifeTimeScale = data.Load("ov_trailLifeTimeScale", ov.trailLifeTimeScale);
    ov.trailScaleMultiplier = data.Load<Vector3>("ov_trailScaleMultiplier", ov.trailScaleMultiplier);
    ov.trailColorMultiplier = data.Load<Vector4>("ov_trailColorMultiplier", ov.trailColorMultiplier);
    ov.trailVelocityScale = data.Load("ov_trailVelocityScale", ov.trailVelocityScale);
    ov.trailInheritVelocity = data.Load("ov_trailInheritVelocity", ov.trailInheritVelocity);
    ov.trailMinLifeTime = data.Load("ov_trailMinLifeTime", ov.trailMinLifeTime);
    ov.enableGather = data.Load("ov_enableGather", ov.enableGather);
    ov.gatherStartRatio = data.Load("ov_gatherStartRatio", ov.gatherStartRatio);
    ov.gatherStrength = data.Load("ov_gatherStrength", ov.gatherStrength);
    ov.gatherTarget = data.Load<Vector3>("ov_gatherTarget", ov.gatherTarget);
    ov.enableVortex = data.Load("ov_enableVortex", ov.enableVortex);
    ov.vortexStrength = data.Load("ov_vortexStrength", ov.vortexStrength);
    ov.vortexAxis = data.Load<Vector3>("ov_vortexAxis", ov.vortexAxis);
    ov.enableAcceleration = data.Load("ov_enableAcceleration", ov.enableAcceleration);
    ov.acceleration = data.Load<Vector3>("ov_acceleration", ov.acceleration);
    ov.enableVelocityDamping = data.Load("ov_enableVelocityDamping", ov.enableVelocityDamping);
    ov.velocityDampingFactor = data.Load("ov_velocityDampingFactor", ov.velocityDampingFactor);
    ov.enableLifetimeVelDamping = data.Load("ov_enableLifetimeVelDamping", ov.enableLifetimeVelDamping);
    ov.lifetimeVelDampingStart = data.Load("ov_lifetimeVelDampingStart", ov.lifetimeVelDampingStart);
    ov.enableCurlNoise = data.Load("ov_enableCurlNoise", ov.enableCurlNoise);
    ov.curlNoiseScale = data.Load("ov_curlNoiseScale", ov.curlNoiseScale);
    ov.curlNoiseStrength = data.Load("ov_curlNoiseStrength", ov.curlNoiseStrength);
    ov.curlNoiseTimeScale = data.Load("ov_curlNoiseTimeScale", ov.curlNoiseTimeScale);
    ov.curlNoiseOctaves = data.Load("ov_curlNoiseOctaves", ov.curlNoiseOctaves);
    ov.curlNoiseAttractStrength = data.Load("ov_curlNoiseAttractStrength", ov.curlNoiseAttractStrength);
    ov.curlNoiseBlendMode = data.Load("ov_curlNoiseBlendMode", ov.curlNoiseBlendMode);
    ov.curlNoisePosRandom = data.Load("ov_curlNoisePosRandom", ov.curlNoisePosRandom);
}

void ParticleCSFieldManager::SaveField(const ParticleField &field) {
    // フォルダ: resources/jsons/ParticleField/  ファイル名: field.name.json
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleField", field.name);
    SaveFieldData(*data, field);
    ImGuiNotification::Post("パーティクルフィールドを保存しました: " + field.name, {0.2f, 0.8f, 0.2f, 1.0f});
}

ParticleField ParticleCSFieldManager::LoadField(const std::string &fileName, const ParticleField &defaultField) {
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleField", fileName);
    if (!data->Exists()) {
        return defaultField;
    }
    ParticleField field = defaultField;
    LoadFieldData(*data, field);
    ImGuiNotification::Post("パーティクルフィールドを読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
    return field;
}

// =============================================
// CreateField
// =============================================

ParticleField *ParticleCSFieldManager::CreateField(const std::string &name, const std::string &templateName) {
    // 上限チェック
    if (static_cast<uint32_t>(fields_.size()) >= kMaxFields) {
        return nullptr;
    }

    ParticleField newField;

    if (!templateName.empty()) {
        // ★ テンプレートjsonが指定されていれば、そのデータを複製して土台にする
        newField = LoadField(templateName, ParticleField{});
    }

    // 名前は引数で上書き（テンプレートの名前ではなく指定名を使う）
    newField.name = name;

    // 自身のjsonが既に存在すれば、それをロードして上書きする
    // （再起動後の復元など、name.json が保存済みの場合に対応）
    {
        std::unique_ptr<DataHandler> selfData = std::make_unique<DataHandler>("ParticleField", name);
        if (selfData->Exists()) {
            LoadFieldData(*selfData, newField);
            newField.name = name; // name だけは引数を優先
        }
    }

    fields_.push_back(newField);
    ImGuiNotification::Post("パーティクルフィールドを作成しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
    return &fields_.back();
}

// =============================================
// ImGui
// =============================================
void ParticleCSFieldManager::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.16f, 0.18f, 0.22f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(420, 600), ImGuiCond_FirstUseEver);

    bool show = true;

    if (!ImGui::Begin("パーティクルフィールド管理", &show, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }
    ImGui::PopStyleColor();

    // ヘッダー情報
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::Text("フィールド数: %d / %d", static_cast<int>(fields_.size()), kMaxFields);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // フィールド追加ボタン
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.40f, 0.30f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.38f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.58f, 0.44f, 1.0f));
    if (ImGui::Button("フィールドを追加", ImVec2(-1, 30))) {
        ParticleField newField;
        newField.name = "Field_" + std::to_string(fields_.size());
        newField.enabled = true;
        AddField(newField);
    }
    ImGui::PopStyleColor(3);
    ImGui::Spacing();

    // フィールドリスト
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(fields_.size()); ++i) {
        auto &f = fields_[i];

        // フィールドタイプ別の色
        ImVec4 headerColor;
        const char *typeLabel;
        switch (static_cast<ParticleFieldType>(f.data.fieldType)) {
        case ParticleFieldType::Wind:
            headerColor = ImVec4(0.30f, 0.40f, 0.52f, 0.55f);
            typeLabel = "[風]";
            break;
        case ParticleFieldType::Attract:
            headerColor = ImVec4(0.42f, 0.34f, 0.50f, 0.55f);
            typeLabel = "[引力]";
            break;
        case ParticleFieldType::Repel:
            headerColor = ImVec4(0.52f, 0.40f, 0.28f, 0.55f);
            typeLabel = "[斥力]";
            break;
        case ParticleFieldType::Vortex:
            headerColor = ImVec4(0.30f, 0.46f, 0.44f, 0.55f);
            typeLabel = "[渦巻き]";
            break;
        default:
            headerColor = ImVec4(0.32f, 0.33f, 0.36f, 0.55f);
            typeLabel = "[不明]";
            break;
        }

        // 無効時はグレーアウト
        if (!f.enabled) {
            headerColor = ImVec4(0.28f, 0.28f, 0.30f, 0.55f);
        }

        ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(headerColor.x + 0.1f, headerColor.y + 0.1f, headerColor.z + 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(headerColor.x + 0.2f, headerColor.y + 0.2f, headerColor.z + 0.2f, 1.0f));

        std::string label = std::string(typeLabel) + " " + f.name + "##field" + std::to_string(i);
        bool open = ImGui::CollapsingHeader(label.c_str());
        ImGui::PopStyleColor(3);

        if (open) {
            ImGui::Indent();
            ImGui::PushItemWidth(200.0f);

            // 有効/無効チェック
            ImGui::Checkbox(("有効##en" + std::to_string(i)).c_str(), &f.enabled);
            ImGui::SameLine();

            // 保存ボタン
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.34f, 0.48f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.44f, 0.60f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.36f, 0.52f, 0.70f, 1.0f));
            if (ImGui::Button(("保存##save" + std::to_string(i)).c_str(), ImVec2(50, 0))) {
                SaveField(f);
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();

            // 削除ボタン
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.46f, 0.24f, 0.24f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.30f, 0.30f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.66f, 0.36f, 0.36f, 1.0f));
            if (ImGui::Button(("削除##del" + std::to_string(i)).c_str(), ImVec2(60, 0))) {
                removeIndex = i;
            }
            ImGui::PopStyleColor(3);

            ImGui::Spacing();

            // 名前
            char nameBuf[128];
            strncpy_s(nameBuf, f.name.c_str(), sizeof(nameBuf) - 1);
            if (ImGui::InputText(("名前##nm" + std::to_string(i)).c_str(), nameBuf, sizeof(nameBuf))) {
                f.name = nameBuf;
            }

            ImGui::Spacing();
            ImGui::Separator();

            // フィールドタイプ選択
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("フィールド種類");
            ImGui::PopStyleColor();

            const char *typeItems[] = {"風 (Wind)", "引力 (Attract)", "斥力 (Repel)", "渦巻き (Vortex)"};
            int typeIdx = static_cast<int>(f.data.fieldType);
            if (ImGui::Combo(("##type" + std::to_string(i)).c_str(), &typeIdx, typeItems, 4)) {
                f.data.fieldType = static_cast<uint32_t>(typeIdx);
            }

            ImGui::Spacing();
            ImGui::Separator();

            // 位置・範囲
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("位置・影響範囲");
            ImGui::PopStyleColor();

            ImGui::DragFloat3(("位置##pos" + std::to_string(i)).c_str(), &f.data.position.x, 0.1f, -9999.0f, 9999.0f, "%.2f");
            ImGui::DragFloat(("影響半径##rad" + std::to_string(i)).c_str(), &f.data.radius, 0.1f, 0.01f, 9999.0f, "%.2f");
            ImGui::DragFloat(("減衰指数##fal" + std::to_string(i)).c_str(), &f.data.falloff, 0.05f, 0.1f, 4.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("1.0=線形減衰  2.0=二乗減衰（端に近いほど弱くなる）");

            ImGui::Spacing();
            ImGui::Separator();

            // タイプ別パラメータ
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("フィールドパラメータ");
            ImGui::PopStyleColor();

            ImGui::DragFloat(("強さ##str" + std::to_string(i)).c_str(), &f.data.strength, 0.05f, -999.0f, 999.0f, "%.3f");

            auto ft = static_cast<ParticleFieldType>(f.data.fieldType);
            if (ft == ParticleFieldType::Wind) {
                ImGui::DragFloat3(("方向##dir" + std::to_string(i)).c_str(), &f.data.direction.x, 0.01f, -1.0f, 1.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("正規化しなくてもシェーダー側で正規化されます");
            } else if (ft == ParticleFieldType::Vortex) {
                ImGui::DragFloat3(("回転軸##dir" + std::to_string(i)).c_str(), &f.data.direction.x, 0.01f, -1.0f, 1.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("渦の回転軸（例: 0,1,0 = Y軸回り）");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // -----------------------------------------------
            // 寿命ドレイン
            // -----------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("寿命ドレイン");
            ImGui::PopStyleColor();

            bool lifeDrainEnabled = (f.data.enableLifeDrain != 0);
            if (ImGui::Checkbox(("有効##ld" + std::to_string(i)).c_str(), &lifeDrainEnabled)) {
                f.data.enableLifeDrain = lifeDrainEnabled ? 1u : 0u;
            }
            if (lifeDrainEnabled) {
                ImGui::DragFloat(("ドレイン量(秒/秒)##ldr" + std::to_string(i)).c_str(),
                                 &f.data.lifeTimeDrain, 0.05f, 0.0f, 100.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("フィールド中心(influence=1)で毎秒この量だけ寿命を消費します");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // -----------------------------------------------
            // トレイル強制生成
            // -----------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("トレイル強制生成");
            ImGui::PopStyleColor();

            bool forceTrail = (f.data.enableForceTrail != 0);
            if (ImGui::Checkbox(("有効##ft" + std::to_string(i)).c_str(), &forceTrail)) {
                f.data.enableForceTrail = forceTrail ? 1u : 0u;
            }
            if (forceTrail) {
                ImGui::DragFloat(("生成間隔上書き##tdo" + std::to_string(i)).c_str(),
                                 &f.data.trailSpawnDistanceOverride, 0.01f, 0.0f, 10.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("0 のときはグループ設定の trailSpawnDistance を使用します");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // -----------------------------------------------
            // カラー乗算
            // -----------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("カラー乗算");
            ImGui::PopStyleColor();

            bool colorMul = (f.data.enableColorMultiply != 0);
            if (ImGui::Checkbox(("有効##cm" + std::to_string(i)).c_str(), &colorMul)) {
                f.data.enableColorMultiply = colorMul ? 1u : 0u;
            }
            if (colorMul) {
                ImGui::ColorEdit4(("乗算色##clr" + std::to_string(i)).c_str(),
                                  &f.data.colorMultiplier.x,
                                  ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("フィールド影響度(influence)でブレンドされます\n白(1,1,1,1)=変化なし");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // -----------------------------------------------
            // 一度きり設定上書き
            // -----------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("一度きり設定上書き");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("フィールドに最初に入ったとき、チェックした\nParticleCSSettingsの項目を書き換えます。\n一度書き換えたパーティクルは再度入っても変化しません。");

            bool settingsOvEnabled = (f.data.enableSettingsOverride != 0);
            if (ImGui::Checkbox(("有効##so" + std::to_string(i)).c_str(), &settingsOvEnabled)) {
                f.data.enableSettingsOverride = settingsOvEnabled ? 1u : 0u;
            }
            if (settingsOvEnabled) {
                DrawOverrideImGui(f.override_, i);
            }

            ImGui::Spacing();
            ImGui::Separator();

            // --- Emit時スポーン判定 ---
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("Emit スポーン判定");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "有効にすると、このフィールドの範囲内に座標がある\n"
                    "パーティクルのみEmitされます。\n"
                    "エミッター全体ではなく触れた部分だけに生成したい場合に使います。\n"
                    "大きさ・間隔はエミッター側の設定に従います。");

            bool emitSpawn = (f.data.enableEmitSpawn != 0);
            if (ImGui::Checkbox(("有効##es" + std::to_string(i)).c_str(), &emitSpawn)) {
                f.data.enableEmitSpawn = emitSpawn ? 1u : 0u;
            }
            if (emitSpawn) {
                ImGui::Indent();
                ImGui::PushItemWidth(180.0f);

                int spawnCount = (int)f.data.emitSpawnCount;
                if (ImGui::DragInt(("発生数/秒##esCount" + std::to_string(i)).c_str(), &spawnCount, 100, 0, 500000)) {
                    f.data.emitSpawnCount = (uint32_t)std::max(0, spawnCount);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("0 の場合はエミッター側の emitCount をそのまま使います。");

                ImGui::DragFloat(("寿命 Min##esLTMin" + std::to_string(i)).c_str(),
                                 &f.data.emitSpawnLifeTimeMin, 0.01f, 0.0f, 60.0f, "%.2f s");
                ImGui::DragFloat(("寿命 Max##esLTMax" + std::to_string(i)).c_str(),
                                 &f.data.emitSpawnLifeTimeMax, 0.01f, 0.0f, 60.0f, "%.2f s");
                // Min > Max にならないよう補正
                if (f.data.emitSpawnLifeTimeMin > f.data.emitSpawnLifeTimeMax)
                    f.data.emitSpawnLifeTimeMin = f.data.emitSpawnLifeTimeMax;

                ImGui::PopItemWidth();
                ImGui::Unindent();
            }

            ImGui::Spacing();
            ImGui::Separator();

            // --- グループID ---
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("グループID");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "-1 = 全エミッターに影響する（デフォルト）\n"
                    "0以上 = 同じIDを持つエミッターにのみ影響する\n"
                    "エミッター側は SetFieldGroupId() で設定します。");
            ImGui::PushItemWidth(120.0f);
            int gid = f.data.groupId;
            if (ImGui::DragInt(("##groupId" + std::to_string(i)).c_str(), &gid, 1, -1, 255)) {
                f.data.groupId = std::max(-1, gid);
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (f.data.groupId == -1) {
                ImGui::TextDisabled("(全エミッター対象)");
            } else {
                ImGui::Text("(ID: %d のエミッターのみ)", f.data.groupId);
            }

            ImGui::PopItemWidth();
            ImGui::Unindent();
        }

        ImGui::Spacing();
    }

    if (removeIndex >= 0) {
        RemoveField(removeIndex);
    }

    // -----------------------------------------------
    // ギズモ表示トグル & 即時描画
    // -----------------------------------------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("デバッグ表示");
    ImGui::PopStyleColor();
    ImGui::Checkbox("ギズモ表示##gizmo", &showGizmos_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("各フィールドの影響範囲・方向をワイヤーフレームで表示します");

    if (showGizmos_) {
        DrawFieldGizmos();
    }

    ImGui::End();
#endif
}

// =============================================
// DrawOverrideImGui
// =============================================
void ParticleCSFieldManager::DrawOverrideImGui(ParticleFieldSettingsOverride &ov, int idx) {
#ifdef USE_IMGUI
    using namespace ParticleSettingsOverrideBits;
    const std::string s = std::to_string(idx);

    // ビット操作ヘルパー（チェックボックスのトグルに使う）
    auto CheckBit = [&](const char *label, uint64_t bit, auto &value, auto min_v, auto max_v, const char *fmt = "%.3f") {
        bool checked = (ov.overrideMask & bit) != 0;
        if (ImGui::Checkbox((std::string("##cb") + label + s).c_str(), &checked)) {
            if (checked)
                ov.overrideMask |= bit;
            else
                ov.overrideMask &= ~bit;
        }
        ImGui::SameLine();
        if (!checked)
            ImGui::BeginDisabled();
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, float>) {
            ImGui::DragFloat((label + s).c_str(), &value, 0.01f, min_v, max_v, fmt);
        } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, uint32_t>) {
            int v = static_cast<int>(value);
            if (ImGui::DragInt((label + s).c_str(), &v, 1, (int)min_v, (int)max_v))
                value = static_cast<uint32_t>(v);
        }
        if (!checked)
            ImGui::EndDisabled();
    };
    auto CheckBitBool = [&](const char *label, uint64_t bit, uint32_t &flag) {
        bool checked = (ov.overrideMask & bit) != 0;
        if (ImGui::Checkbox((std::string("##cb") + label + s).c_str(), &checked)) {
            if (checked)
                ov.overrideMask |= bit;
            else
                ov.overrideMask &= ~bit;
        }
        ImGui::SameLine();
        bool flagBool = (flag != 0);
        if (!checked)
            ImGui::BeginDisabled();
        if (ImGui::Checkbox((label + s).c_str(), &flagBool))
            flag = flagBool ? 1u : 0u;
        if (!checked)
            ImGui::EndDisabled();
    };
    auto CheckBitVec3 = [&](const char *label, uint64_t bit, Vector3 &v) {
        bool checked = (ov.overrideMask & bit) != 0;
        if (ImGui::Checkbox((std::string("##cb") + label + s).c_str(), &checked)) {
            if (checked)
                ov.overrideMask |= bit;
            else
                ov.overrideMask &= ~bit;
        }
        ImGui::SameLine();
        if (!checked)
            ImGui::BeginDisabled();
        ImGui::DragFloat3((label + s).c_str(), &v.x, 0.01f, -999.0f, 999.0f, "%.3f");
        if (!checked)
            ImGui::EndDisabled();
    };
    auto CheckBitVec4 = [&](const char *label, uint64_t bit, Vector4 &v) {
        bool checked = (ov.overrideMask & bit) != 0;
        if (ImGui::Checkbox((std::string("##cb") + label + s).c_str(), &checked)) {
            if (checked)
                ov.overrideMask |= bit;
            else
                ov.overrideMask &= ~bit;
        }
        ImGui::SameLine();
        if (!checked)
            ImGui::BeginDisabled();
        ImGui::ColorEdit4((label + s).c_str(), &v.x, ImGuiColorEditFlags_Float);
        if (!checked)
            ImGui::EndDisabled();
    };

    ImGui::Indent(12.0f);
    ImGui::TextDisabled("チェックした項目だけ上書きされます");

    // ---- 寿命 ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ 寿命 ]");
    ImGui::PopStyleColor();
    CheckBit("寿命Min##ov", LifeTimeMin, ov.lifeTimeMin, 0.0f, 99999.0f);
    CheckBit("寿命Max##ov", LifeTimeMax, ov.lifeTimeMax, 0.0f, 99999.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("lifeTimeMax は lifeTime を直接上書きします\n例: 100000→1.5");

    // ---- スケール ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ スケール ]");
    ImGui::PopStyleColor();
    CheckBit("スケールMin##ov", ScaleMin, ov.scaleMin, 0.0f, 99.0f);
    CheckBit("スケールMax##ov", ScaleMax, ov.scaleMax, 0.0f, 99.0f);

    // ---- 速度 ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ 速度 ]");
    ImGui::PopStyleColor();
    CheckBitVec3("速度Min##ov", VelocityMin, ov.velocityMin);
    CheckBitVec3("速度Max##ov", VelocityMax, ov.velocityMax);

    // ---- 色 ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ 色 ]");
    ImGui::PopStyleColor();
    CheckBitVec4("開始色##ov", StartColor, ov.startColor);
    CheckBitVec4("終了色##ov", EndColor, ov.endColor);

    // ---- LifetimeScale ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ スケールアニメーション ]");
    ImGui::PopStyleColor();
    CheckBitBool("LifetimeScale有効##ov", EnableLifetimeScale, ov.enableLifetimeScale);
    CheckBitBool("ランダムカラー有効##ov", EnableRandomColor, ov.enableRandomColor);
    CheckBitBool("SinScale有効##ov", EnableSinScale, ov.enableSinScale);
    CheckBit("Sin周波数##ov", SinScaleFrequency, ov.sinScaleFrequency, 0.0f, 100.0f);
    CheckBit("Sin振幅##ov", SinScaleAmplitude, ov.sinScaleAmplitude, 0.0f, 10.0f);

    // ---- 重力 ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ 重力 ]");
    ImGui::PopStyleColor();
    CheckBitBool("重力有効##ov", EnableGravity, ov.enableGravity);
    CheckBitVec3("重力ベクトル##ov", Gravity, ov.gravity);

    // ---- トレイル ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ トレイル ]");
    ImGui::PopStyleColor();
    CheckBitBool("トレイル有効##ov", EnableTrail, ov.enableTrail);
    CheckBit("生成間隔##ov", TrailSpawnDistance, ov.trailSpawnDistance, 0.001f, 10.0f);
    CheckBit("最大数/粒##ov", MaxTrailPerParticle, ov.maxTrailPerParticle, 1.0f, 50.0f);
    CheckBit("寿命スケール##ov", TrailLifeTimeScale, ov.trailLifeTimeScale, 0.0f, 5.0f);
    CheckBitVec3("スケール倍率##ov", TrailScaleMultiplier, ov.trailScaleMultiplier);
    CheckBitVec4("色倍率##ov", TrailColorMultiplier, ov.trailColorMultiplier);
    CheckBit("速度スケール##ov", TrailVelocityScale, ov.trailVelocityScale, 0.0f, 5.0f);
    CheckBitBool("速度継承##ov", TrailInheritVelocity, ov.trailInheritVelocity);
    CheckBit("最小寿命##ov", TrailMinLifeTime, ov.trailMinLifeTime, 0.0f, 10.0f);

    // ---- ギャザー ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ ギャザー ]");
    ImGui::PopStyleColor();
    CheckBitBool("ギャザー有効##ov", EnableGather, ov.enableGather);
    CheckBit("開始比率##ov", GatherStartRatio, ov.gatherStartRatio, 0.0f, 1.0f);
    CheckBit("強度##ovg", GatherStrength, ov.gatherStrength, 0.0f, 100.0f);
    CheckBitVec3("目標座標##ov", GatherTarget, ov.gatherTarget);

    // ---- ヴォルテックス ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ ヴォルテックス ]");
    ImGui::PopStyleColor();
    CheckBitBool("渦有効##ov", EnableVortex, ov.enableVortex);
    CheckBit("渦強度##ov", VortexStrength, ov.vortexStrength, -100.0f, 100.0f);
    CheckBitVec3("渦軸##ov", VortexAxis, ov.vortexAxis);

    // ---- 加速度・減衰 ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ 加速度 / ダンピング ]");
    ImGui::PopStyleColor();
    CheckBitBool("加速度有効##ov", EnableAcceleration, ov.enableAcceleration);
    CheckBitVec3("加速度ベクトル##ov", Acceleration, ov.acceleration);
    CheckBitBool("速度ダンピング有効##ov", EnableVelocityDamping, ov.enableVelocityDamping);
    CheckBit("ダンピング係数##ov", VelocityDampingFactor, ov.velocityDampingFactor, 0.0f, 1.0f);
    CheckBitBool("寿命ダンピング有効##ov", EnableLifetimeVelDamping, ov.enableLifetimeVelDamping);
    CheckBit("開始比率##ovd", LifetimeVelDampingStart, ov.lifetimeVelDampingStart, 0.0f, 1.0f);

    // ---- CurlNoise ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
    ImGui::TextUnformatted("[ CurlNoise ]");
    ImGui::PopStyleColor();
    CheckBitBool("CurlNoise有効##ov", EnableCurlNoise, ov.enableCurlNoise);
    CheckBit("スケール##ovcn", CurlNoiseScale, ov.curlNoiseScale, 0.0f, 100.0f);
    CheckBit("強度##ovcn", CurlNoiseStrength, ov.curlNoiseStrength, 0.0f, 100.0f);
    CheckBit("時間スケール##ovcn", CurlNoiseTimeScale, ov.curlNoiseTimeScale, 0.0f, 100.0f);
    CheckBit("オクターブ##ovcn", CurlNoiseOctaves, ov.curlNoiseOctaves, 1.0f, 8.0f);
    CheckBit("引寄強度##ovcn", CurlNoiseAttractStrength, ov.curlNoiseAttractStrength, 0.0f, 100.0f);
    CheckBit("ブレンドモード##ovcn", CurlNoiseBlendMode, ov.curlNoiseBlendMode, 0.0f, 1.0f);
    CheckBit("位置ランダム##ovcn", CurlNoisePosRandom, ov.curlNoisePosRandom, 0.0f, 100.0f);

    ImGui::Unindent(12.0f);
#endif
}

// =============================================
// ギズモ描画
// =============================================

void ParticleCSFieldManager::DrawFieldGizmos() {
    for (const auto &f : fields_) {
        if (!f.enabled)
            continue;

        // フィールドタイプ別に色を決定
        // strength の絶対値を alpha に反映して強さを視覚化（0.4〜1.0 にクランプ）
        float alpha = std::min(1.0f, 0.4f + std::abs(f.data.strength) * 0.06f);

        Vector4 color;
        auto ft = static_cast<ParticleFieldType>(f.data.fieldType);
        switch (ft) {
        case ParticleFieldType::Wind:
            // 風 → 水色
            color = {0.3f, 0.7f, 1.0f, alpha};
            break;
        case ParticleFieldType::Attract:
            // 引力 → 紫
            color = {0.8f, 0.3f, 1.0f, alpha};
            break;
        case ParticleFieldType::Repel:
            // 斥力 → オレンジ
            color = {1.0f, 0.5f, 0.1f, alpha};
            break;
        case ParticleFieldType::Vortex:
            // 渦巻き → 緑
            color = {0.2f, 1.0f, 0.6f, alpha};
            break;
        default:
            color = {0.6f, 0.6f, 0.6f, alpha};
            break;
        }

        // 影響範囲球（全タイプ共通）
        DrawFieldSphere(f, color);

        // タイプ別の方向・強さ表示
        switch (ft) {
        case ParticleFieldType::Wind:
            DrawWindArrows(f, color);
            break;
        case ParticleFieldType::Attract:
            // inward = true（外→中心向き）
            DrawRadialLines(f, color, true);
            break;
        case ParticleFieldType::Repel:
            // inward = false（中心→外向き）
            DrawRadialLines(f, color, false);
            break;
        case ParticleFieldType::Vortex:
            DrawVortexArcs(f, color);
            break;
        default:
            break;
        }
    }
}

// --- 影響範囲球 ---
void ParticleCSFieldManager::DrawFieldSphere(const ParticleField &field, const Vector4 &color) {
    DrawLine3D::GetInstance()->DrawSphere(field.data.position, color, field.data.radius, 16);
}

// --- Wind：球内に等間隔で方向矢印を描く ---
void ParticleCSFieldManager::DrawWindArrows(const ParticleField &field, const Vector4 &color) {
    const Vector3 &center = field.data.position;
    const float r = field.data.radius;

    // 方向を正規化
    Vector3 dir = field.data.direction;
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-5f)
        return;
    dir.x /= len;
    dir.y /= len;
    dir.z /= len;

    // strength の絶対値で矢印の長さを決定（最大 radius の 0.6 倍）
    float arrowLen = std::min(r * 0.6f, std::abs(field.data.strength) * 0.5f + r * 0.15f);
    // 矢頭サイズ
    float headLen = arrowLen * 0.25f;

    // 球内に 3×3×3 グリッドで矢印を配置
    const int grid = 3;
    float step = r * 1.6f / (grid - 1);
    for (int ix = 0; ix < grid; ++ix) {
        for (int iy = 0; iy < grid; ++iy) {
            for (int iz = 0; iz < grid; ++iz) {
                Vector3 offset = {
                    -r * 0.8f + ix * step,
                    -r * 0.8f + iy * step,
                    -r * 0.8f + iz * step,
                };
                // 球の外側は除外
                float d2 = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
                if (d2 > r * r)
                    continue;

                Vector3 from = {center.x + offset.x, center.y + offset.y, center.z + offset.z};
                Vector3 to = {from.x + dir.x * arrowLen, from.y + dir.y * arrowLen, from.z + dir.z * arrowLen};
                DrawLine3D::GetInstance()->SetPoints(from, to, color);

                // 矢頭：dirに垂直な軸で小さな V 字を描く
                // dir に直交するベクトルを求める
                Vector3 up = {0.0f, 1.0f, 0.0f};
                if (std::abs(dir.y) > 0.9f)
                    up = {1.0f, 0.0f, 0.0f};
                // cross(dir, up)
                Vector3 side = {
                    dir.y * up.z - dir.z * up.y,
                    dir.z * up.x - dir.x * up.z,
                    dir.x * up.y - dir.y * up.x,
                };
                float sLen = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
                if (sLen > 1e-5f) {
                    side.x /= sLen;
                    side.y /= sLen;
                    side.z /= sLen;
                }
                Vector3 headBase = {to.x - dir.x * headLen, to.y - dir.y * headLen, to.z - dir.z * headLen};
                Vector3 h1 = {headBase.x + side.x * headLen * 0.5f, headBase.y + side.y * headLen * 0.5f, headBase.z + side.z * headLen * 0.5f};
                Vector3 h2 = {headBase.x - side.x * headLen * 0.5f, headBase.y - side.y * headLen * 0.5f, headBase.z - side.z * headLen * 0.5f};
                DrawLine3D::GetInstance()->SetPoints(to, h1, color);
                DrawLine3D::GetInstance()->SetPoints(to, h2, color);
            }
        }
    }
}

// --- Attract / Repel：球面から中心、または中心から球面へ向かう放射線 ---
void ParticleCSFieldManager::DrawRadialLines(const ParticleField &field, const Vector4 &color, bool inward) {
    const Vector3 &center = field.data.position;
    const float r = field.data.radius;

    // strength の絶対値で線の長さ割合を決定（0.3〜1.0）
    float ratio = std::min(1.0f, 0.3f + std::abs(field.data.strength) * 0.07f);

    // 正二十面体の頂点方向（12方向）を均一配置の代わりに球面上を均等サンプル
    const int stacks = 4;
    const int slices = 8;
    const float PI = 3.1415926535f;
    for (int si = 0; si < stacks; ++si) {
        float theta = PI * (si + 0.5f) / stacks; // 0 〜 π
        for (int sj = 0; sj < slices; ++sj) {
            float phi = 2.0f * PI * sj / slices;
            Vector3 dir = {
                std::sin(theta) * std::cos(phi),
                std::cos(theta),
                std::sin(theta) * std::sin(phi),
            };
            Vector3 surface = {center.x + dir.x * r, center.y + dir.y * r, center.z + dir.z * r};
            // 線の長さを ratio で縮める（途中まで）
            Vector3 inner = {
                center.x + dir.x * r * (1.0f - ratio),
                center.y + dir.y * r * (1.0f - ratio),
                center.z + dir.z * r * (1.0f - ratio),
            };
            if (inward) {
                // 球面 → 中心方向へ（Attract）
                DrawLine3D::GetInstance()->SetPoints(surface, inner, color);
            } else {
                // 中心 → 球面方向へ（Repel）
                DrawLine3D::GetInstance()->SetPoints(inner, surface, color);
            }
        }
    }
}

// --- Vortex：回転軸周りに螺旋状の円弧を描く ---
void ParticleCSFieldManager::DrawVortexArcs(const ParticleField &field, const Vector4 &color) {
    const Vector3 &center = field.data.position;
    const float r = field.data.radius;

    // 回転軸を正規化
    Vector3 axis = field.data.direction;
    float axLen = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (axLen < 1e-5f)
        return;
    axis.x /= axLen;
    axis.y /= axLen;
    axis.z /= axLen;

    // strength の符号で回転方向を決定、絶対値で螺旋の巻き数を決定
    float sign = (field.data.strength >= 0.0f) ? 1.0f : -1.0f;
    float turns = std::min(1.5f, 0.5f + std::abs(field.data.strength) * 0.1f);

    // 軸に直交するベクトルを生成
    Vector3 up = {0.0f, 1.0f, 0.0f};
    if (std::abs(axis.y) > 0.9f)
        up = {1.0f, 0.0f, 0.0f};
    // right = cross(axis, up)
    Vector3 right = {
        axis.y * up.z - axis.z * up.y,
        axis.z * up.x - axis.x * up.z,
        axis.x * up.y - axis.y * up.x,
    };
    float rLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    right.x /= rLen;
    right.y /= rLen;
    right.z /= rLen;
    // forward = cross(right, axis)
    Vector3 forward = {
        right.y * axis.z - right.z * axis.y,
        right.z * axis.x - right.x * axis.z,
        right.x * axis.y - right.y * axis.x,
    };

    // 高さ方向の異なる3段に円弧を描く
    const int arcLayers = 3;
    const int arcSegments = 24;
    const float PI = 3.1415926535f;
    for (int layer = 0; layer < arcLayers; ++layer) {
        // 各段を軸方向にオフセット（-r*0.5 〜 r*0.5）
        float heightOffset = -r * 0.5f + r * layer / (arcLayers - 1);
        Vector3 layerCenter = {
            center.x + axis.x * heightOffset,
            center.y + axis.y * heightOffset,
            center.z + axis.z * heightOffset,
        };
        // 段ごとに半径を変えて円錐状に見せる
        float layerRadius = r * (0.5f + 0.5f * std::sin(PI * layer / (arcLayers - 1)));

        for (int seg = 0; seg < arcSegments; ++seg) {
            float t1 = sign * 2.0f * PI * turns * seg / arcSegments;
            float t2 = sign * 2.0f * PI * turns * (seg + 1) / arcSegments;

            Vector3 p1 = {
                layerCenter.x + layerRadius * (right.x * std::cos(t1) + forward.x * std::sin(t1)),
                layerCenter.y + layerRadius * (right.y * std::cos(t1) + forward.y * std::sin(t1)),
                layerCenter.z + layerRadius * (right.z * std::cos(t1) + forward.z * std::sin(t1)),
            };
            Vector3 p2 = {
                layerCenter.x + layerRadius * (right.x * std::cos(t2) + forward.x * std::sin(t2)),
                layerCenter.y + layerRadius * (right.y * std::cos(t2) + forward.y * std::sin(t2)),
                layerCenter.z + layerRadius * (right.z * std::cos(t2) + forward.z * std::sin(t2)),
            };
            DrawLine3D::GetInstance()->SetPoints(p1, p2, color);
        }
    }

    // 回転軸そのものを細い線で表示（軸の方向が分かるように）
    Vector3 axisTop = {center.x + axis.x * r * 0.6f, center.y + axis.y * r * 0.6f, center.z + axis.z * r * 0.6f};
    Vector3 axisBot = {center.x - axis.x * r * 0.6f, center.y - axis.y * r * 0.6f, center.z - axis.z * r * 0.6f};
    DrawLine3D::GetInstance()->SetPoints(axisBot, axisTop, color);
}
} // namespace Hagine
