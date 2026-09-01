#define NOMINMAX
#include "ParticleCSEmitter.h"
#include "ParticleCSFieldManager.h"
#include "ParticleCSGroupManager.h"
#include <graphics/pipeline/ComputePipelineManager.h>
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

// エミッター設定の保存・読み込み（JSON）。実行時の処理は ParticleCSEmitter.cpp にある。
namespace Hagine {
void ParticleCSEmitter::SaveSetting()
{
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleCS", name_);

    data->Save("isAuto", isAuto_);
    data->Save("isVisible", isVisible_);
    data->Save("isGizmoSelectable", isGizmoSelectable_);
    data->Save("drawGroup", drawGroup_);
    data->Save("frequency", pEmitterMeshData_->frequency);
    data->Save("frequencyTime", pEmitterMeshData_->frequencyTime);
    data->Save<Vector3>("translate", pEmitterMeshData_->translate);
    // ビルボード合成前の回転を保存する（pEmitterMeshData_->rotation はカメラを含む解決済みの値）
    data->Save<Quaternion>("rotation", baseRotation_);
    data->Save("billboardEmitter", billboardEmitter_);
    data->Save<Vector3>("scale", pEmitterMeshData_->scale);
    data->Save("emitFromSurface", pEmitterMeshData_->emitFromSurface);
    data->Save("modelPath", modelPath_);
    data->Save("primitiveType", static_cast<int>(primitiveType_));
    // プリミティブ形状パラメータ（リング等の分割数・半径）
    data->Save("primitiveDivide", static_cast<int>(primitiveParams_.divide));
    data->Save("primitiveHeightDivide", static_cast<int>(primitiveParams_.heightDivide));
    data->Save("primitiveRingOuter", primitiveParams_.ringOuterRadius);
    data->Save("primitiveRingInner", primitiveParams_.ringInnerRadius);

    // フィールド影響設定
    data->Save("receiveFields", receiveFields_);
    data->Save("fieldGroupId", fieldGroupId_);
    data->Save("emitOnlyOnFieldContact", emitOnlyOnFieldContact_);

    // 発光（動的ポイントライト）設定
    data->Save("lightEnabled", lightEnabled_);
    data->Save<Vector4>("lightColor", lightColor_);
    data->Save("lightIntensity", lightIntensity_);
    data->Save("lightRadius", lightRadius_);
    data->Save("lightDecay", lightDecay_);
    data->Save<Vector3>("lightOffset", lightOffset_);
    data->Save("lightFollowParticles", lightFollowParticles_);

    // 粒子ごとの発光（粒子1個1個を光源にする）設定
    data->Save("particleLightEnabled", particleLightEnabled_);
    data->Save("particleLightStride", static_cast<int>(particleLightStride_));
    data->Save("particleLightMaxCount", static_cast<int>(particleLightMaxCount_));
    data->Save("particleLightUseParticleColor", particleLightUseParticleColor_);
    data->Save<Vector4>("particleLightColor", particleLightColor_);
    data->Save("particleLightIntensity", particleLightIntensity_);
    data->Save("particleLightRadius", particleLightRadius_);
    data->Save("particleLightDecay", particleLightDecay_);
    data->Save("particleLightCullDistance", particleLightCullDistance_);

    data->Save("particleGroupCount", static_cast<int>(particleGroups_.size()));

    for (int i = 0; i < particleGroups_.size(); i++)
    {
        auto &group = particleGroups_[i];
        std::string prefix = "group_" + std::to_string(i) + "_";

        data->Save(prefix + "name", group->GetGroupName());
        data->Save(prefix + "minLifetime", group->GetSettingsData()->lifeTimeMin);
        data->Save(prefix + "maxLifetime", group->GetSettingsData()->lifeTimeMax);
        data->Save(prefix + "minScale", group->GetSettingsData()->scaleMin);
        data->Save(prefix + "maxScale", group->GetSettingsData()->scaleMax);
        data->Save(prefix + "minVelocity", group->GetSettingsData()->velocityMin);
        data->Save(prefix + "maxVelocity", group->GetSettingsData()->velocityMax);
        data->Save(prefix + "startColor", group->GetSettingsData()->startColor);
        data->Save(prefix + "endColor", group->GetSettingsData()->endColor);
        data->Save(prefix + "enableLifetimeScale", group->GetSettingsData()->enableLifetimeScale);
        data->Save(prefix + "enableRandomColor", group->GetSettingsData()->enableRandomColor);
        data->Save(prefix + "enableSinScale", group->GetSettingsData()->enableSinScale);
        data->Save(prefix + "sinScaleFrequency", group->GetSettingsData()->sinScaleFrequency);
        data->Save(prefix + "sinScaleAmplitude", group->GetSettingsData()->sinScaleAmplitude);
        data->Save(prefix + "emitCount", group->GetSettingsData()->emitCount);
        data->Save(prefix + "enableGravity", group->GetSettingsData()->enableGravity);
        data->Save(prefix + "gravity", group->GetSettingsData()->gravity);
        data->Save(prefix + "blendMode", static_cast<int>(group->GetParticleGroupData().blendMode));
        // テクスチャ差し替えを永続化する（materials は settings とは別なので個別に保存）。
        {
            ParticleCSGroupData gd = group->GetParticleGroupData();
            data->Save(prefix + "texture", gd.materials.empty() ? std::string("") : gd.materials[0].textureFilePath);
        }

        // ★ トレイル設定の保存
        data->Save(prefix + "enableTrail", group->GetSettingsData()->enableTrail);
        data->Save(prefix + "trailSpawnDistance", group->GetSettingsData()->trailSpawnDistance);
        data->Save(prefix + "maxTrailPerParticle", group->GetSettingsData()->maxTrailPerParticle);
        data->Save(prefix + "trailLifeTimeScale", group->GetSettingsData()->trailLifeTimeScale);
        data->Save(prefix + "trailScaleMultiplier", group->GetSettingsData()->trailScaleMultiplier);
        data->Save(prefix + "trailColorMultiplier", group->GetSettingsData()->trailColorMultiplier);
        data->Save(prefix + "trailVelocityScale", group->GetSettingsData()->trailVelocityScale);
        data->Save(prefix + "trailInheritVelocity", group->GetSettingsData()->trailInheritVelocity);
        data->Save(prefix + "trailMinLifeTime", group->GetSettingsData()->trailMinLifeTime);

        data->Save(prefix + "enableGather", group->GetSettingsData()->enableGather);
        data->Save(prefix + "gatherStartRatio", group->GetSettingsData()->gatherStartRatio);
        data->Save(prefix + "gatherStrength", group->GetSettingsData()->gatherStrength);
        data->Save(prefix + "gatherTarget", group->GetSettingsData()->gatherTarget);
        data->Save(prefix + "gatherTargetOffset", group->GetSettingsData()->gatherTargetOffset);
        data->Save(prefix + "enableGatherForTrail", group->GetSettingsData()->enableGatherForTrail);
        data->Save(prefix + "enableVortex", group->GetSettingsData()->enableVortex);
        data->Save(prefix + "vortexTarget", group->GetSettingsData()->vortexTarget);
        data->Save(prefix + "vortexTargetOffset", group->GetSettingsData()->vortexTargetOffset);
        data->Save(prefix + "vortexStrength", group->GetSettingsData()->vortexStrength);
        data->Save(prefix + "enableVortexForTrail", group->GetSettingsData()->enableVortexForTrail);
        // 保存するのは基準空間での軸（vortexAxis は毎フレーム作られる解決済みワールド値）。
        // キー名は従来どおりなので、旧データ・旧ビルドとそのまま行き来できる。
        data->Save(prefix + "vortexAxis", group->GetSettingsData()->vortexAxisBase);
        data->Save(prefix + "effectSpace", group->GetSettingsData()->effectSpace);

        data->Save(prefix + "enableAcceleration", group->GetSettingsData()->enableAcceleration);
        data->Save(prefix + "acceleration", group->GetSettingsData()->acceleration);
        data->Save(prefix + "enableVelocityDamping", group->GetSettingsData()->enableVelocityDamping);
        data->Save(prefix + "velocityDampingFactor", group->GetSettingsData()->velocityDampingFactor);
        data->Save(prefix + "enableLifetimeVelocityDamping", group->GetSettingsData()->enableLifetimeVelocityDamping);
        data->Save(prefix + "lifetimeVelocityDampingStart", group->GetSettingsData()->lifetimeVelocityDampingStart);
        data->Save(prefix + "enableRadialVelocity", group->GetSettingsData()->enableRadialVelocity);
        data->Save(prefix + "radialVelocityStrength", group->GetSettingsData()->radialVelocityStrength);
        data->Save(prefix + "radialVelocityRandomness", group->GetSettingsData()->radialVelocityRandomness);
        data->Save(prefix + "radialVelocityCenter", group->GetSettingsData()->radialVelocityCenter);

        data->Save(prefix + "enableCurlNoise", group->GetSettingsData()->enableCurlNoise);
        data->Save(prefix + "curlNoiseScale", group->GetSettingsData()->curlNoiseScale);
        data->Save(prefix + "curlNoiseStrength", group->GetSettingsData()->curlNoiseStrength);
        data->Save(prefix + "curlNoiseTimeScale", group->GetSettingsData()->curlNoiseTimeScale);
        data->Save(prefix + "curlNoiseOctaves", group->GetSettingsData()->curlNoiseOctaves);
        data->Save(prefix + "curlNoiseAttractStrength", group->GetSettingsData()->curlNoiseAttractStrength);
        data->Save(prefix + "curlNoiseBlendMode", group->GetSettingsData()->curlNoiseBlendMode);
        data->Save(prefix + "curlNoisePosRandomStrength", group->GetSettingsData()->curlNoisePosRandomStrength);
        data->Save(prefix + "curlNoiseAttractCenter", group->GetSettingsData()->curlNoiseAttractCenter);

        // ★ 終了スケール設定の保存
        data->Save(prefix + "enableEndScale", group->GetSettingsData()->enableEndScale);
        data->Save(prefix + "endScaleValue", group->GetSettingsData()->endScaleValue);

        // ★ 回転設定の保存
        data->Save(prefix + "enableRandomRotation", group->GetSettingsData()->enableRandomRotation);
        data->Save<Vector3>(prefix + "rotationMin", group->GetSettingsData()->rotationMin);
        data->Save<Vector3>(prefix + "rotationMax", group->GetSettingsData()->rotationMax);
        data->Save(prefix + "enableRandomAngularVelocity", group->GetSettingsData()->enableRandomAngularVelocity);
        data->Save<Vector3>(prefix + "angularVelocityMin", group->GetSettingsData()->angularVelocityMin);
        data->Save<Vector3>(prefix + "angularVelocityMax", group->GetSettingsData()->angularVelocityMax);

        data->Save(prefix + "enableBillboard", group->GetPerView()->enableBillboard);

        // ★ 速度ストレッチ設定の保存
        data->Save(prefix + "enableVelocityStretch", group->GetPerView()->enableVelocityStretch);
        data->Save(prefix + "velocityStretchFactor", group->GetPerView()->velocityStretchFactor);

        // ★ 描画カリング(overdraw対策)設定の保存
        data->Save(prefix + "enableDistanceCull", group->GetPerView()->enableDistanceCull);
        data->Save(prefix + "distanceCullStart", group->GetPerView()->distanceCullStart);
        data->Save(prefix + "distanceCullEnd", group->GetPerView()->distanceCullEnd);
        data->Save(prefix + "enableSizeClamp", group->GetPerView()->enableSizeClamp);
        data->Save(prefix + "maxScreenHeight", group->GetPerView()->maxScreenHeight);
        data->Save(prefix + "minScreenHeight", group->GetPerView()->minScreenHeight);

        // ★ GPU駆動の視錐台カリング（既定ON。切ると画面外の粒子も描画リストに載る）
        data->Save(prefix + "enableFrustumCull", static_cast<uint32_t>(group->IsFrustumCullEnabled() ? 1 : 0));

        // ★ 中間カラー設定の保存
        data->Save(prefix + "enableMidColor", group->GetSettingsData()->enableMidColor);
        data->Save(prefix + "midColorRatio", group->GetSettingsData()->midColorRatio);
        data->Save(prefix + "midColor", group->GetSettingsData()->midColor);

        // ★ タービュランス設定の保存
        data->Save(prefix + "enableTurbulence", group->GetSettingsData()->enableTurbulence);
        data->Save(prefix + "turbulenceStrength", group->GetSettingsData()->turbulenceStrength);
        data->Save(prefix + "turbulenceFrequency", group->GetSettingsData()->turbulenceFrequency);

        // ★ 音声振動設定の保存（audioAmplitude は実行時注入のエンベロープなので保存しない）
        data->Save(prefix + "enableAudioVibration", group->GetSettingsData()->enableAudioVibration);
        data->Save(prefix + "audioVibrationStrength", group->GetSettingsData()->audioVibrationStrength);
        data->Save(prefix + "audioVibrationSensitivity", group->GetSettingsData()->audioVibrationSensitivity);
        data->Save(prefix + "audioVibrationFrequency", group->GetSettingsData()->audioVibrationFrequency);
        data->Save(prefix + "audioAttackSharpness", group->GetSettingsData()->audioAttackSharpness);
        data->Save(prefix + "audioReleaseRate", group->GetSettingsData()->audioReleaseRate);

        // ★ 発生形状設定の保存
        data->Save(prefix + "emitShape", group->GetSettingsData()->emitShape);
        data->Save(prefix + "emitSphereRadius", group->GetSettingsData()->emitSphereRadius);
        data->Save(prefix + "emitConeAngle", group->GetSettingsData()->emitConeAngle);

        // ★ カラーグラデーション(N段)設定の保存（有効フラグ + ストップ列）
        data->Save(prefix + "enableColorGradient", group->GetSettingsData()->enableColorGradient);
        const auto &stops = group->GetColorStops();
        data->Save(prefix + "colorStopCount", static_cast<int>(stops.size()));
        for (size_t si = 0; si < stops.size(); ++si)
        {
            std::string sp = prefix + "colorStop_" + std::to_string(si) + "_";
            data->Save<Vector4>(sp + "color", stops[si].color);
            data->Save(sp + "pos", stops[si].pos);
        }

        // ★ 寿命カーブ(サイズ/アルファ)設定の保存（有効フラグ + 制御点列）
        data->Save(prefix + "enableSizeCurve", group->GetSettingsData()->enableSizeCurve);
        data->Save(prefix + "enableAlphaCurve", group->GetSettingsData()->enableAlphaCurve);
        auto saveCurve = [&](const std::string &key, const std::vector<CurvePoint> &pts) {
            data->Save(prefix + key + "Count", static_cast<int>(pts.size()));
            for (size_t pi = 0; pi < pts.size(); ++pi)
            {
                std::string pp = prefix + key + "_" + std::to_string(pi) + "_";
                data->Save(pp + "x", pts[pi].x);
                data->Save(pp + "y", pts[pi].y);
            }
        };
        saveCurve("sizeCurve", group->GetSizeCurvePoints());
        saveCurve("alphaCurve", group->GetAlphaCurvePoints());
    }
    ImGuiNotification::Post("パーティクル設定を保存しました: " + name_, {0.2f, 0.8f, 0.2f, 1.0f});
}

void ParticleCSEmitter::LoadSetting()
{
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleCS", name_);

    isAuto_ = data->Load("isAuto", false);
    isVisible_ = data->Load("isVisible", true);
    isGizmoSelectable_ = data->Load("isGizmoSelectable", true);
    drawGroup_ = data->Load<std::string>("drawGroup", "3D");
    if (drawGroup_ != "UI")
    {
        drawGroup_ = "3D"; // 旧データは3D扱いに正規化
    }
    pEmitterMeshData_->frequency = data->Load("frequency", 0.1f);
    pEmitterMeshData_->frequencyTime = data->Load("frequencyTime", 0.0f);
    pEmitterMeshData_->translate = data->Load<Vector3>("translate", Vector3(0.0f, 0.0f, 0.0f));
    baseRotation_ = data->Load<Quaternion>("rotation", Quaternion::IdentityQuaternion());
    billboardEmitter_ = data->Load("billboardEmitter", false);
    pEmitterMeshData_->rotation = baseRotation_; // 次の DrawCompute でビルボードが合成される
    pEmitterMeshData_->scale = data->Load<Vector3>("scale", Vector3(1.0f, 1.0f, 1.0f));
    pEmitterMeshData_->emitFromSurface = data->Load<uint32_t>("emitFromSurface", 1);

    modelPath_ = data->Load("modelPath", std::string(""));
    primitiveType_ = static_cast<PrimitiveType>(data->Load("primitiveType", static_cast<int>(PrimitiveType::None)));
    // プリミティブ形状パラメータ（LoadPrimitiveModel より前に復元しておくこと）
    primitiveParams_.divide = static_cast<uint32_t>(data->Load("primitiveDivide", 32));
    primitiveParams_.heightDivide = static_cast<uint32_t>(data->Load("primitiveHeightDivide", 1));
    primitiveParams_.ringOuterRadius = data->Load("primitiveRingOuter", 1.0f);
    primitiveParams_.ringInnerRadius = data->Load("primitiveRingInner", 0.5f);
    // フィールド影響設定
    receiveFields_ = data->Load("receiveFields", false);
    fieldGroupId_ = data->Load("fieldGroupId", -1);
    emitOnlyOnFieldContact_ = data->Load("emitOnlyOnFieldContact", false);
    // 旧キー fieldContactEmitCount は廃止（発生数はフィールド側の接触Emit設定に一本化）

    // 発光（動的ポイントライト）設定。既定OFFなので旧データの見た目は変わらない
    lightEnabled_ = data->Load("lightEnabled", false);
    lightColor_ = data->Load<Vector4>("lightColor", Vector4(1.0f, 0.9f, 0.6f, 1.0f));
    lightIntensity_ = data->Load("lightIntensity", 2.0f);
    lightRadius_ = data->Load("lightRadius", 8.0f);
    lightDecay_ = data->Load("lightDecay", 1.0f);
    lightOffset_ = data->Load<Vector3>("lightOffset", Vector3(0.0f, 0.0f, 0.0f));
    lightFollowParticles_ = data->Load("lightFollowParticles", true);

    // 粒子ごとの発光。こちらも既定OFFなので旧データの見た目は変わらない
    particleLightEnabled_ = data->Load("particleLightEnabled", false);
    particleLightStride_ = static_cast<uint32_t>((std::max)(1, data->Load("particleLightStride", 8)));
    particleLightMaxCount_ = static_cast<uint32_t>((std::max)(0, data->Load("particleLightMaxCount", 64)));
    particleLightUseParticleColor_ = data->Load("particleLightUseParticleColor", true);
    particleLightColor_ = data->Load<Vector4>("particleLightColor", Vector4(1.0f, 0.9f, 0.6f, 1.0f));
    particleLightIntensity_ = data->Load("particleLightIntensity", 1.0f);
    particleLightRadius_ = data->Load("particleLightRadius", 3.0f);
    particleLightDecay_ = data->Load("particleLightDecay", 1.0f);
    particleLightCullDistance_ = data->Load("particleLightCullDistance", 60.0f);

    if (!modelPath_.empty())
    {
        LoadModel(modelPath_);
        CreateModelTriangles();
    }
    else if (primitiveType_ != PrimitiveType::None)
    {
        LoadPrimitiveModel(primitiveType_);
        CreateModelTriangles();
        CreateModelEdges();
    }

    groupNum_ = data->Load("particleGroupCount", 0);
    for (int i = 0; i < groupNum_; i++)
    {
        std::string prefix = "group_" + std::to_string(i) + "_";
        std::string groupName = data->Load(prefix + "name", std::string(""));

        auto group = ParticleCSGroupManager::GetInstance()->GetIndependentParticleGroup(groupName);
        if (!group)
            continue;

        ParticleCSSettings settings;
        settings.lifeTimeMin = data->Load(prefix + "minLifetime", 1.0f);
        settings.lifeTimeMax = data->Load(prefix + "maxLifetime", 1.0f);
        settings.scaleMin = data->Load(prefix + "minScale", 1.0f);
        settings.scaleMax = data->Load(prefix + "maxScale", 1.0f);
        settings.velocityMin = data->Load<Vector3>(prefix + "minVelocity", {0.0f, 0.0f, 0.0f});
        settings.velocityMax = data->Load<Vector3>(prefix + "maxVelocity", {0.0f, 0.0f, 0.0f});
        settings.startColor = data->Load(prefix + "startColor", Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        settings.endColor = data->Load(prefix + "endColor", Vector4(1.0f, 1.0f, 1.0f, 0.0f));
        settings.enableLifetimeScale = data->Load<uint32_t>(prefix + "enableLifetimeScale", 0);
        settings.enableRandomColor = data->Load<uint32_t>(prefix + "enableRandomColor", 0);
        settings.enableSinScale = data->Load<uint32_t>(prefix + "enableSinScale", 0);
        settings.sinScaleFrequency = data->Load(prefix + "sinScaleFrequency", 5.0f);
        settings.sinScaleAmplitude = data->Load(prefix + "sinScaleAmplitude", 0.3f);
        settings.emitCount = data->Load<uint32_t>(prefix + "emitCount", 10);
        settings.enableGravity = data->Load<uint32_t>(prefix + "enableGravity", false);
        settings.gravity = data->Load<Vector3>(prefix + "gravity", {0.0f, 0.0f, 0.0f});
        settings.maxParticleCount = group->GetMaxParticleCount();

        // ★ トレイル設定のロード
        settings.enableTrail = data->Load<uint32_t>(prefix + "enableTrail", 0);
        settings.trailSpawnDistance = data->Load(prefix + "trailSpawnDistance", 0.1f);
        settings.maxTrailPerParticle = data->Load<uint32_t>(prefix + "maxTrailPerParticle", 5);
        settings.trailLifeTimeScale = data->Load(prefix + "trailLifeTimeScale", 1.0f);
        settings.trailScaleMultiplier = data->Load<Vector3>(prefix + "trailScaleMultiplier", {0.8f, 0.8f, 0.8f});
        settings.trailColorMultiplier = data->Load(prefix + "trailColorMultiplier", Vector4(1.0f, 1.0f, 1.0f, 0.7f));
        settings.trailVelocityScale = data->Load(prefix + "trailVelocityScale", 0.3f);
        settings.trailInheritVelocity = data->Load<uint32_t>(prefix + "trailInheritVelocity", 1);
        settings.trailMinLifeTime = data->Load(prefix + "trailMinLifeTime", 0.5f);

        settings.enableGather = data->Load(prefix + "enableGather", 0);
        settings.gatherStartRatio = data->Load(prefix + "gatherStartRatio", 0.5f);
        settings.gatherStrength = data->Load(prefix + "gatherStrength", 1.0f);
        settings.gatherTarget = data->Load<Vector3>(prefix + "gatherTarget", {0.0f, 0.0f, 0.0f});
        settings.gatherTargetOffset = data->Load<Vector3>(prefix + "gatherTargetOffset", {0.0f, 0.0f, 0.0f});
        settings.enableGatherForTrail = data->Load<uint32_t>(prefix + "enableGatherForTrail", 0);
        settings.enableVortex = data->Load<uint32_t>(prefix + "enableVortex", 0);
        settings.vortexTarget = data->Load<Vector3>(prefix + "vortexTarget", {0.0f, 0.0f, 0.0f});
        settings.vortexTargetOffset = data->Load<Vector3>(prefix + "vortexTargetOffset", {0.0f, 0.0f, 0.0f});
        settings.vortexStrength = data->Load(prefix + "vortexStrength", 1.0f);
        settings.enableVortexForTrail = data->Load<uint32_t>(prefix + "enableVortexForTrail", 0);
        // "vortexAxis" は基準空間での軸として読む（解決済みの settings.vortexAxis は毎フレーム作り直される）。
        settings.vortexAxisBase = data->Load<Vector3>(prefix + "vortexAxis", {0.0f, 1.0f, 0.0f});
        settings.vortexAxis = settings.vortexAxisBase;
        settings.effectSpace = data->Load<uint32_t>(prefix + "effectSpace", 0);

        settings.enableAcceleration = data->Load<uint32_t>(prefix + "enableAcceleration", 0);
        settings.acceleration = data->Load<Vector3>(prefix + "acceleration", {0.0f, 0.0f, 0.0f});
        settings.enableVelocityDamping = data->Load<uint32_t>(prefix + "enableVelocityDamping", 0);
        settings.velocityDampingFactor = data->Load(prefix + "velocityDampingFactor", 0.0f);
        settings.enableLifetimeVelocityDamping = data->Load<uint32_t>(prefix + "enableLifetimeVelocityDamping", 0);
        settings.lifetimeVelocityDampingStart = data->Load(prefix + "lifetimeVelocityDampingStart", 0.0f);
        settings.enableRadialVelocity = data->Load<uint32_t>(prefix + "enableRadialVelocity", 0);
        settings.radialVelocityStrength = data->Load(prefix + "radialVelocityStrength", 0.0f);
        settings.radialVelocityRandomness = data->Load(prefix + "radialVelocityRandomness", 0.0f);
        settings.radialVelocityCenter = data->Load<Vector3>(prefix + "radialVelocityCenter", {0.0f, 0.0f, 0.0f});

        settings.enableCurlNoise = data->Load<uint32_t>(prefix + "enableCurlNoise", 0);
        settings.curlNoiseScale = data->Load(prefix + "curlNoiseScale", 1.0f);
        settings.curlNoiseStrength = data->Load(prefix + "curlNoiseStrength", 1.0f);
        settings.curlNoiseTimeScale = data->Load(prefix + "curlNoiseTimeScale", 1.0f);
        settings.curlNoiseOctaves = data->Load<uint32_t>(prefix + "curlNoiseOctaves", 1);
        settings.curlNoiseAttractStrength = data->Load(prefix + "curlNoiseAttractStrength", 0.0f);
        settings.curlNoiseBlendMode = data->Load<uint32_t>(prefix + "curlNoiseBlendMode", 0);
        settings.curlNoisePosRandomStrength = data->Load(prefix + "curlNoisePosRandomStrength", 0.0f);
        settings.curlNoiseAttractCenter = data->Load<Vector3>(prefix + "curlNoiseAttractCenter", {0.0f, 0.0f, 0.0f});

        // ★ 終了スケール設定のロード
        settings.enableEndScale = data->Load<uint32_t>(prefix + "enableEndScale", 0);
        settings.endScaleValue = data->Load<Vector3>(prefix + "endScaleValue", {0.0f, 0.0f, 0.0f});

        // ★ 回転設定のロード
        settings.enableRandomRotation = data->Load<uint32_t>(prefix + "enableRandomRotation", 0);
        settings.rotationMin = data->Load<Vector3>(prefix + "rotationMin", {0.0f, 0.0f, 0.0f});
        settings.rotationMax = data->Load<Vector3>(prefix + "rotationMax", {0.0f, 0.0f, 0.0f});
        settings.enableRandomAngularVelocity = data->Load<uint32_t>(prefix + "enableRandomAngularVelocity", 0);
        settings.angularVelocityMin = data->Load<Vector3>(prefix + "angularVelocityMin", {0.0f, 0.0f, 0.0f});
        settings.angularVelocityMax = data->Load<Vector3>(prefix + "angularVelocityMax", {0.0f, 0.0f, 0.0f});

        group->SetBillboard(data->Load(prefix + "enableBillboard", true));

        // ★ 速度ストレッチ設定のロード
        group->GetPerView()->enableVelocityStretch = data->Load<uint32_t>(prefix + "enableVelocityStretch", 0);
        group->GetPerView()->velocityStretchFactor = data->Load(prefix + "velocityStretchFactor", 0.1f);

        // ★ 描画カリング(overdraw対策)設定のロード
        group->GetPerView()->enableDistanceCull = data->Load<uint32_t>(prefix + "enableDistanceCull", 0);
        group->GetPerView()->distanceCullStart = data->Load(prefix + "distanceCullStart", 50.0f);
        group->GetPerView()->distanceCullEnd = data->Load(prefix + "distanceCullEnd", 100.0f);
        group->GetPerView()->enableSizeClamp = data->Load<uint32_t>(prefix + "enableSizeClamp", 0);
        group->GetPerView()->maxScreenHeight = data->Load(prefix + "maxScreenHeight", 1.0f);
        group->GetPerView()->minScreenHeight = data->Load(prefix + "minScreenHeight", 0.0f);

        // ★ GPU駆動の視錐台カリング（キーが無い既存Jsonは既定ON＝軽量化が効いた状態）
        group->SetFrustumCullEnabled(data->Load<uint32_t>(prefix + "enableFrustumCull", 1) != 0);

        // ★ 中間カラー設定のロード
        settings.enableMidColor = data->Load<uint32_t>(prefix + "enableMidColor", 0);
        settings.midColorRatio = data->Load(prefix + "midColorRatio", 0.5f);
        settings.midColor = data->Load(prefix + "midColor", Vector4(1.0f, 1.0f, 1.0f, 1.0f));

        // ★ タービュランス設定のロード
        settings.enableTurbulence = data->Load<uint32_t>(prefix + "enableTurbulence", 0);
        settings.turbulenceStrength = data->Load(prefix + "turbulenceStrength", 1.0f);
        settings.turbulenceFrequency = data->Load(prefix + "turbulenceFrequency", 2.0f);

        // ★ 音声振動設定のロード（audioAmplitude は実行時注入のエンベロープなので既定のまま）
        settings.enableAudioVibration = data->Load<uint32_t>(prefix + "enableAudioVibration", 0);
        settings.audioVibrationStrength = data->Load(prefix + "audioVibrationStrength", 12.0f);
        settings.audioVibrationSensitivity = data->Load(prefix + "audioVibrationSensitivity", 4.0f);
        settings.audioVibrationFrequency = data->Load(prefix + "audioVibrationFrequency", 22.0f);
        settings.audioAttackSharpness = data->Load(prefix + "audioAttackSharpness", 1.8f);
        settings.audioReleaseRate = data->Load(prefix + "audioReleaseRate", 10.0f);

        // ★ 発生形状設定のロード
        settings.emitShape = data->Load<uint32_t>(prefix + "emitShape", 0);
        settings.emitSphereRadius = data->Load(prefix + "emitSphereRadius", 1.0f);
        settings.emitConeAngle = data->Load(prefix + "emitConeAngle", 0.5236f);

        // ★ カラーグラデーション(N段)設定のロード（有効フラグは settings、ストップは group 側ストレージ）
        settings.enableColorGradient = data->Load<uint32_t>(prefix + "enableColorGradient", 0);
        // ★ 寿命カーブ(サイズ/アルファ)の有効フラグ（点は group 側ストレージ）
        settings.enableSizeCurve = data->Load<uint32_t>(prefix + "enableSizeCurve", 0);
        settings.enableAlphaCurve = data->Load<uint32_t>(prefix + "enableAlphaCurve", 0);

        group->SetSettingData(settings);

        {
            int stopCount = data->Load(prefix + "colorStopCount", 0);
            if (stopCount > 0)
            {
                auto &stops = group->GetColorStops();
                stops.clear();
                for (int si = 0; si < stopCount; ++si)
                {
                    std::string sp = prefix + "colorStop_" + std::to_string(si) + "_";
                    GradientStop gs;
                    gs.color = data->Load<Vector4>(sp + "color", Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                    gs.pos = data->Load(sp + "pos", 0.0f);
                    stops.push_back(gs);
                }
                group->MarkColorStopsDirty();
            }
            // 寿命カーブの制御点をロード（サイズ/アルファ）。
            auto loadCurve = [&](const std::string &key, std::vector<CurvePoint> &out) {
                int cnt = data->Load(prefix + key + "Count", 0);
                if (cnt <= 0)
                    return;
                out.clear();
                for (int pi = 0; pi < cnt; ++pi)
                {
                    std::string pp = prefix + key + "_" + std::to_string(pi) + "_";
                    CurvePoint cp;
                    cp.x = data->Load(pp + "x", 0.0f);
                    cp.y = data->Load(pp + "y", 1.0f);
                    out.push_back(cp);
                }
            };
            loadCurve("sizeCurve", group->GetSizeCurvePoints());
            loadCurve("alphaCurve", group->GetAlphaCurvePoints());
            group->MarkLifeCurvesDirty();
        }
        group->SetBlendMode(static_cast<BlendMode>(data->Load<int>(prefix + "blendMode", static_cast<int>(BlendMode::Add))));
        // 保存済みテクスチャがあれば適用（無ければグループ定義のテクスチャを維持）。
        // AddParticleGroup がこの group のテクスチャを描画対象の独立グループへ伝播する。
        {
            std::string tex = data->Load<std::string>(prefix + "texture", "");
            if (!tex.empty())
                group->SetTexture(tex);
        }

        AddParticleGroup(group);
    }
    ImGuiNotification::Post("パーティクル設定を読み込みました: " + name_, {0.2f, 0.8f, 0.8f, 1.0f});
}

} // namespace Hagine
