#pragma once
#include "type/Vector3.h"
#include "type/Vector4.h"
#include <cstdint>

/// =====================================================================
/// ParticleCSSettings 上書き用ビット定数
/// フィールド側の overrideMask / パーティクル側の settingsOverrideFlags
/// で共通使用する。
/// uint64_t なので最大64項目まで対応。
/// =====================================================================
namespace Hagine {
namespace ParticleSettingsOverrideBits {
static constexpr uint64_t LifeTimeMin = 1ULL << 0;
static constexpr uint64_t LifeTimeMax = 1ULL << 1;
static constexpr uint64_t ScaleMin = 1ULL << 2;
static constexpr uint64_t ScaleMax = 1ULL << 3;
static constexpr uint64_t VelocityMin = 1ULL << 4;
static constexpr uint64_t VelocityMax = 1ULL << 5;
static constexpr uint64_t StartColor = 1ULL << 6;
static constexpr uint64_t EndColor = 1ULL << 7;
static constexpr uint64_t EnableLifetimeScale = 1ULL << 8;
static constexpr uint64_t EnableRandomColor = 1ULL << 9;
static constexpr uint64_t EnableSinScale = 1ULL << 10;
static constexpr uint64_t SinScaleFrequency = 1ULL << 11;
static constexpr uint64_t SinScaleAmplitude = 1ULL << 12;
static constexpr uint64_t EnableGravity = 1ULL << 13;
static constexpr uint64_t Gravity = 1ULL << 14;
static constexpr uint64_t EnableTrail = 1ULL << 15;
static constexpr uint64_t TrailSpawnDistance = 1ULL << 16;
static constexpr uint64_t MaxTrailPerParticle = 1ULL << 17;
static constexpr uint64_t TrailLifeTimeScale = 1ULL << 18;
static constexpr uint64_t TrailScaleMultiplier = 1ULL << 19;
static constexpr uint64_t TrailColorMultiplier = 1ULL << 20;
static constexpr uint64_t TrailVelocityScale = 1ULL << 21;
static constexpr uint64_t TrailInheritVelocity = 1ULL << 22;
static constexpr uint64_t TrailMinLifeTime = 1ULL << 23;
static constexpr uint64_t EnableGather = 1ULL << 24;
static constexpr uint64_t GatherStartRatio = 1ULL << 25;
static constexpr uint64_t GatherStrength = 1ULL << 26;
static constexpr uint64_t GatherTarget = 1ULL << 27;
static constexpr uint64_t EnableVortex = 1ULL << 28;
static constexpr uint64_t VortexStrength = 1ULL << 29;
static constexpr uint64_t VortexAxis = 1ULL << 30;
static constexpr uint64_t EnableAcceleration = 1ULL << 31;
static constexpr uint64_t Acceleration = 1ULL << 32;
static constexpr uint64_t EnableVelocityDamping = 1ULL << 33;
static constexpr uint64_t VelocityDampingFactor = 1ULL << 34;
static constexpr uint64_t EnableLifetimeVelDamping = 1ULL << 35;
static constexpr uint64_t LifetimeVelDampingStart = 1ULL << 36;
static constexpr uint64_t EnableCurlNoise = 1ULL << 37;
static constexpr uint64_t CurlNoiseScale = 1ULL << 38;
static constexpr uint64_t CurlNoiseStrength = 1ULL << 39;
static constexpr uint64_t CurlNoiseTimeScale = 1ULL << 40;
static constexpr uint64_t CurlNoiseOctaves = 1ULL << 41;
static constexpr uint64_t CurlNoiseAttractStrength = 1ULL << 42;
static constexpr uint64_t CurlNoiseBlendMode = 1ULL << 43;
static constexpr uint64_t CurlNoisePosRandom = 1ULL << 44;
// 必要があれば 45〜63 を追加可能
} // namespace ParticleSettingsOverrideBits
} // namespace Hagine
