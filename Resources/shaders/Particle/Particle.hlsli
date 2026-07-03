struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct Particle
{
    float3 translate;
    float3 scale;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
    float3 initialScale;
    float padding;
    uint isTrailParticle;
    uint parentIndex;
    float3 lastTrailPosition;
    float trailSpawnDistance;
    uint2 settingsOverrideFlags;
    // 回転 (XYZ軸回転をラジアンで保持)
    float3 rotation;
    float paddingRot;
    float3 angularVelocity;
    float paddingAngVel;
    // 終了スケール (enableEndScale=1 のとき lifeRatio で initialScale→endScale を lerp)
    float3 endScale;
    float paddingScale;
};

// =============================================
// SoA バッファ要素（C++ ParticleStruct.h の CSParticleXxx と**バイト単位で一致**）
//   StructuredBuffer は 4 バイト境界のタイトパッキング（float3=12B / float4=16B）。
//   gLife=float(4B) / gDrawCore=PDrawCore(36B) / gSimCore=PSimCore(12B)
//   gTrail=PTrail(20B) / gRotation=PRotation(24B) / gOverride=uint2(8B)
//   上記サイズは C++ 側 static_assert が固定している。
//   scale 系は half pack（PackScaleXY/Z, UnpackScale3）、color は RGBA8 pack。
// =============================================
struct PDrawCore
{
    float3 translate; // 12B
    uint scaleXY;     //  4B half(x)|half(y)<<16
    uint scaleZ;      //  4B half(z)（上位16bitは空き）
    float3 velocity;  // 12B
    uint color;       //  4B RGBA8 パック（Pack/UnpackColorRGBA8 で変換）
};

struct PSimCore
{
    float currentTime;          // 4B
    uint initialScaleXY;        // 4B half(x)|half(y)<<16
    uint initialScaleZ_isTrail; // 4B 下位16bit=half(z) / 上位16bit=isTrailParticle(0/1)
};

struct PTrail
{
    uint parentIndex;
    float3 lastTrailPosition;
    float trailSpawnDistance;
};

struct PRotation
{
    float3 rotation;
    float3 angularVelocity;
};

// color を RGBA8(unorm) 1 word に圧縮/復元する。
//   バッファ境界(load/store)でのみ使い、計算は従来通り float4 で行う。
//   範囲は [0,1] にクランプ（粒子色は加算/通常ブレンド前提で十分）。
uint PackColorRGBA8(float4 c)
{
    uint4 q = (uint4) (saturate(c) * 255.0f + 0.5f);
    return q.r | (q.g << 8) | (q.b << 16) | (q.a << 24);
}

float4 UnpackColorRGBA8(uint p)
{
    return float4(p & 0xFFu, (p >> 8) & 0xFFu, (p >> 16) & 0xFFu, (p >> 24) & 0xFFu) * (1.0f / 255.0f);
}

// scale 系（描画scale / initialScale）を half3 にパック/復元する。
//   視覚用途のみで半精度(fp16)で十分。translate/velocity には使わない
//   （位置精度の低下・速度積分のドリフト/ジッタを避けるため）。
//   StructuredBuffer は 4B 境界なので half3(6B) は置けず、f32tof16 で
//   xy=1word / z=1word に詰める。z word の上位16bit は空き
//   （SimCore では isTrailParticle をそこへ同梱する）。
uint PackScaleXY(float3 s)
{
    return f32tof16(s.x) | (f32tof16(s.y) << 16);
}
uint PackScaleZ(float3 s)
{
    return f32tof16(s.z);
}
// z word（DrawCore は scaleZ、SimCore は initialScaleZ_isTrail）の下位16bitのみ参照。
float3 UnpackScale3(uint xy, uint z)
{
    return float3(f16tof32(xy & 0xFFFFu), f16tof32(xy >> 16), f16tof32(z & 0xFFFFu));
}
// SimCore の z word: 下位16bit=half(z) / 上位16bit=isTrailParticle(0/1)。
uint PackScaleZTrail(float3 s, uint isTrail)
{
    return f32tof16(s.z) | ((isTrail & 0xFFFFu) << 16);
}

Particle CreateEmptyParticle()
{
    Particle p;
    p.translate = float3(0, 0, 0);
    p.scale = float3(0, 0, 0);
    p.lifeTime = 0.0f;
    p.velocity = float3(0, 0, 0);
    p.currentTime = 0.0f;
    p.color = float4(0, 0, 0, 0);
    p.initialScale = float3(0, 0, 0);
    p.padding = 0.0f;
    p.isTrailParticle = 0;
    p.parentIndex = 0xFFFFFFFF;
    p.settingsOverrideFlags = uint2(0, 0);
    p.rotation = float3(0, 0, 0);
    p.paddingRot = 0.0f;
    p.angularVelocity = float3(0, 0, 0);
    p.paddingAngVel = 0.0f;
    p.endScale = float3(0, 0, 0);
    p.paddingScale = 0.0f;
    return p;
}

// =============================================
// 生存判定の正準述語
//   Update 側で死亡したパーティクルは lifeTime を 0 にリセットするため、
//   未使用スロット・死亡スロットは共に lifeTime <= 0 となる。
//   生存コンパクション(aliveList)と描画カリングはこの述語に一本化する。
// =============================================
bool IsAliveParticle(Particle p)
{
    return p.lifeTime > 0.0f;
}

bool HasOverrideBit(uint2 flags, uint bitIndex)
{
    if (bitIndex < 32u)
        return (flags.x & (1u << bitIndex)) != 0u;
    else
        return (flags.y & (1u << (bitIndex - 32u))) != 0u;
}

void SetOverrideBit(inout uint2 flags, uint bitIndex)
{
    if (bitIndex < 32u)
        flags.x |= (1u << bitIndex);
    else
        flags.y |= (1u << (bitIndex - 32u));
}

struct PerView
{
    float4x4 viewProjection;
    float4x4 billboardMatrix;
    uint enableBillboard;
    uint enableVelocityStretch;
    float velocityStretchFactor;
    uint enableRotation; // 1=回転あり / 0=回転なし（VSで回転行列計算をスキップ）
    // ---- 描画カリング (overdraw 対策)。C++ PerView と一致させること ----
    float3 cameraPosition;     // 距離計算用カメラワールド座標
    uint enableDistanceCull;   // 1=距離フェード+カリング
    float distanceCullStart;   // フェード開始距離
    float distanceCullEnd;     // 完全カリング距離
    float projScaleY;          // projection[1][1]（画面サイズ計算用）
    uint enableSizeClamp;      // 1=画面サイズ上限+微小カリング
    float maxScreenHeight;     // 画面上の最大高さ(NDC)。超過分はスケール縮小
    float minScreenHeight;     // これ未満の画面高さは微小カリング(0=無効)
    float drawCullPad0;
    float drawCullPad1;
};

struct EmitterMesh
{
    float3 translate;
    uint triangleCount;
    float4 rotation;
    uint emitFromSurface;
    float3 scale;
    float frequency;
    float frequencyTime;
    uint emit;
    uint edgeCount;
    float3 anchorPoint;
    // 発生数ゲートの上書き値。0=通常(gSettings.emitCount)、>0=この値を発生数に使う（フィールド接触Emit用）
    uint emitCountOverride;
};

struct PerFrame
{
    float time;
    float deltaTime;
    int groupId;
    // エミッターが影響を受けるフィールドグループID
    // -1 = 全フィールド対象, 0以上 = 同IDのフィールドのみ対象
    int emitterFieldGroupId;
};

struct ParticleCSSettings
{
    float lifeTimeMin;
    float lifeTimeMax;
    float scaleMin;
    float scaleMax;
    float3 velocityMin;
    float padding1;
    float3 velocityMax;
    float padding2;
    float4 startColor;
    float4 endColor;
    uint enableLifetimeScale;
    uint enableRandomColor;
    uint enableSinScale;
    uint emitCount;
    uint maxParticleCount;
    float sinScaleFrequency;
    float sinScaleAmplitude;
    uint enableGravity;
    float3 gravity;
    uint enableTrail;
    float trailSpawnDistance;
    uint maxTrailPerParticle;
    float trailLifeTimeScale;
    float paddingTrail;
    float3 trailScaleMultiplier;
    float padding3;
    float4 trailColorMultiplier;
    float trailVelocityScale;
    uint trailInheritVelocity;
    float trailMinLifeTime;
    float padding4;
    uint enableGather;
    float gatherStartRatio;
    float gatherStrength;
    float padding5;
    float3 gatherTarget;
    float padding6;
    float3 gatherTargetOffset;
    uint enableGatherForTrail;
    uint enableVortex;
    float3 vortexTarget;
    float3 vortexTargetOffset;
    float vortexStrength;
    uint enableVortexForTrail;
    float3 vortexAxis;
    uint enableAcceleration;
    float3 acceleration;
    uint enableVelocityDamping;
    float velocityDampingFactor;
    uint enableLifetimeVelocityDamping;
    float lifetimeVelocityDampingStart;
    uint enableRadialVelocity;
    float radialVelocityStrength;
    float radialVelocityRandomness;
    float padding7;
    float3 radialVelocityCenter;
    float padding8;
    uint enableCurlNoise;
    float curlNoiseScale;
    float curlNoiseStrength;
    float curlNoiseTimeScale;
    uint curlNoiseOctaves;
    float curlNoiseAttractStrength;
    uint curlNoiseBlendMode;
    float curlNoisePosRandomStrength;
    float3 curlNoiseAttractCenter;
    float padding9;
    // ---- 終了スケール ----
    uint enableEndScale; // 1=有効: lifeRatio で initialScale→endScale を lerp
    float3 endScaleValue; // 終了時のスケール値 (XYZ 個別指定)
    // ---- 回転 ----
    uint enableRandomRotation; // 1=発生時にランダムな初期角度を設定
    float3 rotationMin; // 初期角度の最小値 (ラジアン, XYZ)
    float3 rotationMax; // 初期角度の最大値 (ラジアン, XYZ)
    float paddingRotMax;
    uint enableRandomAngularVelocity; // 1=発生時にランダムな角速度を設定
    float3 angularVelocityMin; // 角速度の最小値 (ラジアン/秒, XYZ)
    float paddingAngVelMin;
    float3 angularVelocityMax; // 角速度の最大値 (ラジアン/秒, XYZ)
    // ---- 中間カラー (3-stop gradient) ----
    uint enableMidColor;
    float midColorRatio;
    float padMidColor0;
    float padMidColor1;
    float4 midColor;
    // ---- タービュランス ----
    uint enableTurbulence;
    float turbulenceStrength;
    float turbulenceFrequency;
    float turbulencePad;
    // ---- 発生形状 ----
    uint emitShape;          // 0=Box, 1=Sphere Surface, 2=Cone
    float emitSphereRadius;
    float emitConeAngle;
    float emitShapePad;
    // ---- カラーグラデーション (N段 LUT を CB 同梱)。C++ ParticleCSSettings と一致させること ----
    uint enableColorGradient; // 1=colorLUT を色に使う（start/mid/end/random を上書き）
    float colorGradPad0;
    float colorGradPad1;
    float colorGradPad2;
    uint4 colorLUT[64];       // 256段 RGBA8。idx の色 = colorLUT[idx>>2][idx&3]
    // ---- 寿命カーブ(サイズ/アルファ倍率) LUT。C++ ParticleCSSettings と一致させること ----
    uint enableSizeCurve;     // 1=scale に sizeCurveLUT を乗算
    uint enableAlphaCurve;    // 1=color.a に alphaCurveLUT を乗算
    float lifeCurvePad0;
    float lifeCurvePad1;
    float4 sizeCurveLUT[64];  // 256段 float 倍率。idx の倍率 = sizeCurveLUT[idx>>2][idx&3]
    float4 alphaCurveLUT[64];
    // ---- 音声振動（音の立ち上がりでバンっと揺らす）。C++ ParticleCSSettings 末尾と一致させること ----
    uint enableAudioVibration;       // 1=今流れている音量で各粒子を揺らす
    float audioVibrationStrength;    // 振動の大きさ（揺れ幅）
    float audioVibrationSensitivity; // 感度（音の立ち上がりに掛ける入力ゲイン）
    float audioAmplitude;            // 音の立ち上がりエンベロープ[0,1]（CPU注入。ビートで跳ね時間で減衰＝振動の駆動）
    float audioVibrationFrequency;   // 振動の速さ（Hz的スケール。大きいほど細かく震える）
    float audioAttackSharpness;      // 反応カーブ指数（>1で大きい音だけドンと反応・小さい音は無視）
    float audioReleaseRate;          // エンベロープ減衰速度[1/s]（CPUが使用。shaderは未使用）
    float audioPad0;                 // 16B境界パディング
};

// 【重要】このレイアウトは C++ 側 `struct ParticleFieldData`
//   （Engine/3d/Particle/ParticleStruct.h）と**バイト単位で一致**させること（合計112バイト）。
//   C++ 側には sizeof/offsetof の static_assert があり、ずれるとビルドで検出される。
struct ParticleField
{
    // 1. Force（速度系）
    float3 position;
    float radius;
    float3 direction;
    float strength;
    uint fieldType;
    float falloff;
    
    float lifeTimeDrain;
    uint enableLifeDrain;
    
    uint enableForceTrail;
    float trailSpawnDistanceOverride;
    
    uint enableColorMultiply;
    float4 colorMultiplier;
    
    uint enableSettingsOverride;

    // --- Emit時スポーン判定 ---
    // 1 のとき、このフィールドの範囲内にEmit座標があるパーティクルのみ発生させる
    uint enableEmitSpawn;
    float emitSpawnLifeTimeMin;
    float emitSpawnLifeTimeMax;
    uint emitSpawnCount;

    // グループID (-1=全エミッター対象, 0以上=同IDのエミッターのみ)
    int groupId;
    float groupIdPadding0;
    float groupIdPadding1;
    float groupIdPadding2;
};

static const uint OB_LifeTimeMin = 0u;
static const uint OB_LifeTimeMax = 1u;
static const uint OB_ScaleMin = 2u;
static const uint OB_ScaleMax = 3u;
static const uint OB_VelocityMin = 4u;
static const uint OB_VelocityMax = 5u;
static const uint OB_StartColor = 6u;
static const uint OB_EndColor = 7u;
static const uint OB_EnableLifetimeScale = 8u;
static const uint OB_EnableRandomColor = 9u;
static const uint OB_EnableSinScale = 10u;
static const uint OB_SinScaleFrequency = 11u;
static const uint OB_SinScaleAmplitude = 12u;
static const uint OB_EnableGravity = 13u;
static const uint OB_Gravity = 14u;
static const uint OB_EnableTrail = 15u;
static const uint OB_TrailSpawnDistance = 16u;
static const uint OB_MaxTrailPerParticle = 17u;
static const uint OB_TrailLifeTimeScale = 18u;
static const uint OB_TrailScaleMultiplier = 19u;
static const uint OB_TrailColorMultiplier = 20u;
static const uint OB_TrailVelocityScale = 21u;
static const uint OB_TrailInheritVelocity = 22u;
static const uint OB_TrailMinLifeTime = 23u;
static const uint OB_EnableGather = 24u;
static const uint OB_GatherStartRatio = 25u;
static const uint OB_GatherStrength = 26u;
static const uint OB_GatherTarget = 27u;
static const uint OB_EnableVortex = 28u;
static const uint OB_VortexStrength = 29u;
static const uint OB_VortexAxis = 30u;
static const uint OB_EnableAcceleration = 31u;
static const uint OB_Acceleration = 32u;
static const uint OB_EnableVelocityDamping = 33u;
static const uint OB_VelocityDampingFactor = 34u;
static const uint OB_EnableLifetimeVelDamping = 35u;
static const uint OB_LifetimeVelDampingStart = 36u;
static const uint OB_EnableCurlNoise = 37u;
static const uint OB_CurlNoiseScale = 38u;
static const uint OB_CurlNoiseStrength = 39u;
static const uint OB_CurlNoiseTimeScale = 40u;
static const uint OB_CurlNoiseOctaves = 41u;
static const uint OB_CurlNoiseAttractStrength = 42u;
static const uint OB_CurlNoiseBlendMode = 43u;
static const uint OB_CurlNoisePosRandom = 44u;

struct ParticleFieldSettingsOverrideData
{
   
    uint2 overrideMask;
    float2 maskPadding;

    float lifeTimeMin;
    float lifeTimeMax;
    float scaleMin;
    float scaleMax;

    float3 velocityMin;
    float padding1;
    float3 velocityMax;
    float padding2;

    float4 startColor;
    float4 endColor;

    uint enableLifetimeScale;
    uint enableRandomColor;
    uint enableSinScale;
    float sinScaleFrequency;
    float sinScaleAmplitude;

    uint enableGravity;
    float2 padding3;
    float3 gravity;
    float padding4;

    uint enableTrail;
    float trailSpawnDistance;
    uint maxTrailPerParticle;
    float trailLifeTimeScale;

    float3 trailScaleMultiplier;
    float padding5;
    float4 trailColorMultiplier;
    float trailVelocityScale;
    uint trailInheritVelocity;
    float trailMinLifeTime;
    float padding6;

    uint enableGather;
    float gatherStartRatio;
    float gatherStrength;
    float padding7;
    float3 gatherTarget;
    float padding8;

    uint enableVortex;
    float vortexStrength;
    float2 padding9;
    float3 vortexAxis;
    float padding10;

    uint enableAcceleration;
    float3 padding11;
    float3 acceleration;
    float padding12;

    uint enableVelocityDamping;
    float velocityDampingFactor;
    uint enableLifetimeVelDamping;
    float lifetimeVelDampingStart;

    uint enableCurlNoise;
    float curlNoiseScale;
    float curlNoiseStrength;
    float curlNoiseTimeScale;
    uint curlNoiseOctaves;
    float curlNoiseAttractStrength;
    uint curlNoiseBlendMode;
    float curlNoisePosRandom;
};

struct FieldCountCB
{
    uint fieldCount;
    float pad0;
    float pad1;
    float pad2;
};

struct EdgeInfo
{
    float3 v0;
    float padding0;
    float3 v1;
    float padding1;
};

struct TriangleInfo
{
    float3 v0;
    float padding0;
    float3 v1;
    float padding1;
    float3 v2;
    float padding2;
};