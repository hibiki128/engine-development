#pragma once
#include "graphics/pipeline/PipelineManager.h"
#include "type/Matrix4x4.h"
#include "type/Vector3.h"
#include "type/Vector4.h"
#include <model/ModelStructs.h>
#include <transform/WorldTransform.h>
#include <cstddef> // offsetof
#include <cstdint>
#include <d3d12.h>
#include <list>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// パーティクルのマテリアル情報（色・UV変換・テクスチャ）
/// </summary>
struct ParticleMaterial
{
    Vector4 color;
    Matrix4x4 uvTransform;
    // ---- プロシージャル形状（画像を用意せず、ピクセルシェーダーで形そのものを作る）----
    // HLSL 側 struct Material（ParticleCS.PS.hlsl）とここまでを一致させること。
    // ※ この構造体の先頭から textureFilePath の手前までが GPU へ渡る領域
    uint32_t shapeMode = 0;      // 0=テクスチャそのまま / 1=炎 / 2=シャード(稲妻) / 3=リング
    float shapeEdge = 0.5f;      // 輪郭のしきい値。大きいほど痩せる
    float shapeNoiseScale = 3.0f;// ゆらぎの細かさ
    float shapeRimWidth = 0.22f; // 縁取りの太さ（0で縁取りなし）
    Vector4 shapeRimColor = {0.45f, 0.85f, 1.0f, 1.0f}; // 縁の色（中心は白く抜ける）
    float shapeTime = 0.0f;      // ランタイム: 経過時間（CPUが毎フレーム入れる）
    float shapeSpeed = 1.6f;     // ゆらぎが動く速さ
    float shapeSoftness = 0.06f; // 輪郭のぼかし。0 に近いほどカチッとしたセル調になる
    uint32_t enableSoftParticle = 0; // 1=背景に近いほど薄くして刺さった断面を消す
    float softParticleFade = 1.0f;   // 完全に消えるまでの距離（ワールド単位）
    // 深度を「ビュー空間の奥行き」へ戻すのに要る射影行列の2要素。
    //   viewZ = m32 / (深度バッファの値 - m22)
    // 近/遠クリップを持ち回るより、これ2つの方が取り違えが起きない
    float depthProjM22 = 0.0f;
    float depthProjM32 = 0.0f;
    float shapeFresnel = 0.0f;   // 縁ほど濃く・正面ほど薄くする強さ [0,1]（0で無効）
    Vector2 uvScrollSpeed = {0.0f, 0.0f}; // UVを流す速さ（炎が立ち上る動きに使う）
    float shapePad2 = 0.0f;
    float shapePad3 = 0.0f;
    // ↑ここまでが GPU へ行く
    std::string textureFilePath;
    uint32_t textureIndex = 0;
};

/// ===== GPUParticle =====

/// <summary>
/// GPUパーティクルの発生源メッシュ情報
/// </summary>
struct EmitterMesh
{
    Vector3 translate;
    uint32_t triangleCount;
    Quaternion rotation;
    uint32_t emitFromSurface;
    Vector3 scale;
    float frequency;
    float frequencyTime;
    uint32_t emit;
    uint32_t edgeCount;
    Vector3 anchorPoint;
    // 発生数ゲートの上書き値。0=通常(グループの gSettings.emitCount を使用)、
    // >0=この値を1フレームの発生数として使う（フィールド接触Emitモード用）。
    // gSettings.emitCount を一時書き換え＋即復元する方式は GPU 実行時に復元後の値を
    // 読んでしまい効かなかったため、per-emitter CB のこの専用フィールドで渡す。
    uint32_t emitCountOverride;
    // 発生数へ掛ける係数[0,1]。発生の立ち上がり（スポーン率のフェードイン）に使う。
    // 1.0 で従来どおり。**グループごとの emitCount を書き換えるのではなくここで掛ける**こと。
    // 書き換えてしまうと、作者が決めた値が実行時の値で上書きされるうえ、
    // 「emitCount が 0 のグループはディスパッチごと省く」最適化に引っかかって
    // 係数が 0 の瞬間にグループが止まり、二度と復帰しなくなる
    float emitRateScale;
};

/// <summary>
/// GPU上で管理する1パーティクル分のデータ（旧AoSレイアウト）。
/// SoA化(下の CSParticleXxx 群)に伴い GPU バッファとしては未使用になったが、
/// ParticleCSGroupData::particles（CPU側 std::list、現状未使用）の要素型として残置。
/// </summary>
struct CSParticle
{
    Vector3 translate;
    Vector3 scale;
    float lifeTime;
    Vector3 velocity;
    float currentTime;
    Vector4 color;
    Vector3 initialScale;
    float padding;
    uint32_t isTrailParticle;
    uint32_t parentIndex;
    Vector3 lastTrailPosition;
    float trailSpawnDistance;
    uint32_t settingsOverrideFlagsLo = 0;
    uint32_t settingsOverrideFlagsHi = 0;
    Vector3 rotation = {0.0f, 0.0f, 0.0f};
    float paddingRot = 0.0f;
    Vector3 angularVelocity = {0.0f, 0.0f, 0.0f};
    float paddingAngVel = 0.0f;
    Vector3 endScale = {0.0f, 0.0f, 0.0f};
    float paddingScale = 0.0f;
};

/// =============================================================
/// GPUパーティクル SoA（Structure of Arrays）レイアウト
///
/// 旧 156B 単一バッファ(CSParticle)を機能別バッファに分割し、
/// Update CS が「使う機能のバッファだけ load/store」できるようにして
/// メモリ帯域を削減する（演出なし構成で 156B → 76B）。
///
/// 【重要】各構造体は HLSL 側（Engine/EngineAssets/shaders/Particle/Particle.hlsli の
///   PDrawCore / PSimCore / PTrail / PRotation 等）と**バイト単位で一致**させること。
///   StructuredBuffer は 4 バイト境界のタイトパッキング（float3=12B, float4=16B）。
///   末尾の static_assert がレイアウト契約を固定する。
///
///   gLife      : float            (4B)  — 生存判定。死亡/未使用スロットは早期returnで4Bのみ
///   gDrawCore  : CSParticleDrawCore(36B) — translate/scale(half3)/velocity/color(RGBA8)（描画VSもここを読む）
///   gSimCore   : CSParticleSimCore (12B) — currentTime/initialScale(half3)/isTrailParticle
///   gTrail     : CSParticleTrail   (20B) — parentIndex/lastTrailPosition/trailSpawnDistance
///   gRotation  : CSParticleRotation(24B) — rotation/angularVelocity
///   gOverride  : uint2             (8B)  — settingsOverrideFlags(lo/hi)
/// =============================================================

/// 描画コア。translate/scale/velocity/color。Update が常時 load/store し、描画VSも読む。
/// scale は half3 パック(scaleXY=half x|y, scaleZ=half z)、color は RGBA8 パック(uint)。
/// 計算は float で行い、バッファ境界でのみ pack/unpack する（HLSL PackScaleXY/Z 等）。
struct CSParticleDrawCore
{
    Vector3 translate;
    uint32_t scaleXY; // half(x) | half(y)<<16
    uint32_t scaleZ;  // half(z)（上位16bitは空き）
    Vector3 velocity;
    uint32_t color;
};

/// シミュレーションコア。Update が常時 load/store する補助状態。
/// initialScale は half3 パック。isTrailParticle(0/1) は initialScaleZ_isTrail の上位16bitに同梱。
struct CSParticleSimCore
{
    float currentTime;
    uint32_t initialScaleXY;        // half(x) | half(y)<<16
    uint32_t initialScaleZ_isTrail; // 下位16bit=half(z) / 上位16bit=isTrailParticle
};

/// トレイル状態。トレイル機能が有効なときのみ load/store。
struct CSParticleTrail
{
    uint32_t parentIndex;
    Vector3 lastTrailPosition;
    float trailSpawnDistance;
};

/// 回転状態。回転機能が有効なときのみ load/store。描画VSも回転時のみ読む。
struct CSParticleRotation
{
    Vector3 rotation;
    Vector3 angularVelocity;
};

/// 設定上書き完了フラグ（lo=bit0-31 / hi=bit32-63）。フィールド存在時のみ load/store。
struct CSParticleOverride
{
    uint32_t settingsOverrideFlagsLo;
    uint32_t settingsOverrideFlagsHi;
};

// GPUレイアウト契約の固定（HLSL 側とバイト単位で一致させること）。
static_assert(sizeof(CSParticleDrawCore) == 36, "CSParticleDrawCore は36B(scale=half3 pack/color=RGBA8 pack)。HLSL PDrawCore と一致させること");
static_assert(sizeof(CSParticleSimCore) == 12, "CSParticleSimCore は12B(initialScale=half3 pack/isTrailは上位16bit同梱)。HLSL PSimCore と一致させること");
static_assert(sizeof(CSParticleTrail) == 20, "CSParticleTrail は20B。HLSL PTrail と一致させること");
static_assert(sizeof(CSParticleRotation) == 24, "CSParticleRotation は24B。HLSL PRotation と一致させること");
static_assert(sizeof(CSParticleOverride) == 8, "CSParticleOverride は8B。HLSL uint2 と一致させること");

/// <summary>
/// 描画時にビュー単位で渡す情報（ビュープロジェクション・ビルボード設定など）
/// </summary>
struct PerView
{
    Matrix4x4 viewProjection;
    Matrix4x4 billboardMatrix;
    uint32_t enableBillboard = 1;       // 1=ビルボードON(デフォルト), 0=OFF
    uint32_t enableVelocityStretch = 0; // 1=速度方向に引き伸ばす
    float velocityStretchFactor = 0.1f; // 引き伸ばし係数(速さ×係数 = 伸び率)
    // 1=回転あり（VSで回転行列を計算）/ 0=回転なし（VSの sincos×3＋行列積をスキップ）。
    // グループが回転を使わない（enableRandomRotation/enableRandomAngularVelocity が両方OFF）なら
    // 全パーティクルの rotation が常に 0 なので、VS の回転計算を丸ごと省ける。
    uint32_t enableRotation = 0;
    // ---- フリップブック（描画VSがUVをずらすのに使う）----
    // コマ番号そのものは Update/Emit が DrawCore の scaleZ 上位16bit へ詰める。
    // VS はここの格子数を見て「何コマ目をどのUVで出すか」を決める
    uint32_t enableFlipbookDraw = 0;
    uint32_t flipbookColsDraw = 1;
    uint32_t flipbookRowsDraw = 1;
    uint32_t flipbookDrawPad = 0;
    // ---- 描画カリング (overdraw 対策) ----
    // 距離カリング: 遠い粒子をアルファフェード→縮退カリングしてフィルレート(ROP/blend)を節約する。
    // 画面サイズ上限/微小カリング: 巨大粒子のスケールを抑え、サブピクセル粒子を破棄する。
    // HLSL PerView（Particle.hlsli）とバイト単位で一致させること（CB の16B境界straddle無し）。
    Vector3 cameraPosition = {0.0f, 0.0f, 0.0f}; // 距離計算用カメラワールド座標(Update で vp.translation_ をコピー)
    uint32_t enableDistanceCull = 0;             // 1=距離フェード+カリング
    float distanceCullStart = 50.0f;             // この距離からアルファをフェード開始
    float distanceCullEnd = 100.0f;              // この距離で完全カリング(縮退頂点で破棄)
    float projScaleY = 1.0f;                     // projection[1][1]（画面サイズ計算用, Update でコピー）
    uint32_t enableSizeClamp = 0;                // 1=画面サイズ上限+微小カリング
    float maxScreenHeight = 1.0f;                // 画面上の最大高さ(NDC, 2=画面全体)。超過分はスケール縮小
    float minScreenHeight = 0.0f;                // これ未満の画面高さは微小カリング(0=無効)
    float drawCullPad0 = 0.0f;
    float drawCullPad1 = 0.0f;
};

/// <summary>
/// 発生源メッシュの三角形1枚分の頂点情報
/// </summary>
struct TriangleInfo
{
    Vector3 v0;
    float padding0;
    Vector3 v1;
    float padding1;
    Vector3 v2;
    float padding2;
};

/// <summary>
/// 描画時にフレーム単位で渡す情報（時間・グループID など）
/// </summary>
struct PerFrame
{
    float time;
    float deltaTime;
    uint32_t groupId;
    // このグループが受けるフィールドの GPU 上の番号をビットで（bit i = フィールドバッファの i 番目）。
    // レイヤーの一致判定は CPU で済ませておき、シェーダーは立っているビットだけ読む。
    uint32_t fieldUpdateMask; // 粒子を動かす・変える効果を持つフィールド（Update 用）
    uint32_t fieldEmitMask;   // 「この範囲から発生」を持ち、今フレーム発生するフィールド（Emit 用）
    uint32_t perFramePad0;
    uint32_t perFramePad1;
    uint32_t perFramePad2;
};

/// <summary>
/// エミッターデータ（発生源メッシュを保持）
/// </summary>
struct EmitterData
{
    EmitterMesh mesh;
};

/// <summary>
/// メッシュ表面上のサンプル点
/// </summary>
struct SurfacePoint
{
    Vector3 position;
    float padding;
};

/// <summary>
/// 発生源メッシュのエッジ1本分の頂点情報
/// </summary>
struct EdgeInfo
{
    Vector3 v0;
    float padding0;
    Vector3 v1;
    float padding1;
};

/// <summary>
/// GPUパーティクルグループの保持データ（マテリアル・パーティクル一覧など）
/// </summary>
struct ParticleCSGroupData
{
    // マテリアルデータ
    std::vector<ParticleMaterial> materials;
    // パーティクルのリスト (std::list<Particle> 型)
    std::list<CSParticle> particles;
    // グループ名
    std::string groupName;
    // ブレンドモード
    BlendMode blendMode = BlendMode::Add;
};

static const uint32_t kMaxParticleCount = 100000; // 最大パーティクル数
extern uint32_t threadsPerGroup_;                 // 1グループあたりのスレッド数
extern int threadGroupSize_;                      // スレッドグループの数

/// <summary>
/// カラーグラデーションのストップ（位置 pos[0..1] と RGBA）。CPU側のみ保持。
/// colorStops 列を Update 用に 256段 RGBA8 LUT へベイクして ParticleCSSettings.colorLUT に積む
/// （GPU は LUT をサンプルするだけ＝ストップ数に依存しない O(1)）。
/// </summary>
struct GradientStop
{
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    float pos = 0.0f;
};

/// <summary>
/// 寿命カーブの制御点（x=寿命比[0,1], y=倍率）。CPU側のみ保持（ImVec2 と同レイアウト）。
/// サイズ/アルファの倍率カーブを 256段 LUT へベイクして ParticleCSSettings に積む。
/// </summary>
struct CurvePoint
{
    float x = 0.0f; // 寿命比 [0,1]
    float y = 1.0f; // 倍率（1.0=変化なし）
};

/// <summary>
/// GPUパーティクルの挙動設定（寿命・速度・色・各種エフェクトの有効化など）
/// HLSL側 struct ParticleCSSettings とレイアウトを一致させること
/// </summary>
struct ParticleCSSettings
{
    float lifeTimeMin = 1.0f;
    float lifeTimeMax = 3.0f;
    float scaleMin = 0.5f;
    float scaleMax = 1.5f;
    Vector3 velocityMin = {-0.5f, -0.5f, -0.5f};
    float padding1{};
    Vector3 velocityMax = {0.5f, 0.5f, 0.5f};
    float padding2{};
    Vector4 startColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 endColor = {1.0f, 1.0f, 1.0f, 0.0f};
    uint32_t enableLifetimeScale = 0;
    uint32_t enableRandomColor = 0;
    uint32_t enableSinScale = 0;
    uint32_t emitCount = 0;
    uint32_t maxParticleCount = 10000;
    float sinScaleFrequency{};
    float sinScaleAmplitude{};
    uint32_t enableGravity = 0;
    Vector3 gravity = {0.0f, -9.8f, 0.0f};
    uint32_t enableTrail = 0;
    float trailSpawnDistance = 0.1f;
    uint32_t maxTrailPerParticle = 5;
    float trailLifeTimeScale = 0.5f;
    float paddingTrail{};
    Vector3 trailScaleMultiplier = {0.8f, 0.8f, 0.8f};
    float padding3{};
    Vector4 trailColorMultiplier = {1.0f, 1.0f, 1.0f, 0.7f};
    float trailVelocityScale = 0.3f;
    uint32_t trailInheritVelocity = 1;
    float trailMinLifeTime = 0.3f;
    float padding4{};
    uint32_t enableGather = 0;
    float gatherStartRatio = 0.5f;
    float gatherStrength = 2.0f;
    float padding5{};
    Vector3 gatherTarget = {0, 0, 0}; // 解決済みワールド座標（毎フレーム emitter が offset から算出）
    float padding6{};
    Vector3 gatherTargetOffset = {0, 0, 0}; // エミッターからの相対座標（effectSpace の空間で解釈）
    uint32_t enableGatherForTrail = 0;
    uint32_t enableVortex = 0;
    Vector3 vortexTarget = {0, 0, 0};       // 解決済みワールド座標（毎フレーム emitter が offset から算出）
    Vector3 vortexTargetOffset = {0, 0, 0}; // エミッターからの相対座標（effectSpace の空間で解釈）
    float vortexStrength = 5.0f;
    uint32_t enableVortexForTrail = 0;
    // 解決済みワールド回転軸。毎フレーム vortexAxisBase を effectSpace で変換した値が入る
    //（作者が指定する軸は下の vortexAxisBase。こちらを直接編集しても毎フレーム上書きされる）
    Vector3 vortexAxis = {0.0f, 1.0f, 0.0f};
    uint32_t enableAcceleration = 0;
    Vector3 acceleration = {0.0f, 0.0f, 0.0f};
    uint32_t enableVelocityDamping = 0;
    float velocityDampingFactor = 0.0f;
    uint32_t enableLifetimeVelocityDamping = 0;
    float lifetimeVelocityDampingStart = 0.0f;
    uint32_t enableRadialVelocity = 0;
    float radialVelocityStrength = 0.0f;
    float radialVelocityRandomness = 0.0f;
    float padding7{};
    Vector3 radialVelocityCenter = {0.0f, 0.0f, 0.0f};
    float padding8{};
    uint32_t enableCurlNoise;
    float curlNoiseScale;
    float curlNoiseStrength;
    float curlNoiseTimeScale;
    uint32_t curlNoiseOctaves;
    float curlNoiseAttractStrength;
    uint32_t curlNoiseBlendMode;
    float curlNoisePosRandomStrength;
    Vector3 curlNoiseAttractCenter;
    float padding9{};
    // ---- 終了スケール ----
    uint32_t enableEndScale = 0;                // 1=有効: lifeRatio で initialScale→endScale を lerp
    Vector3 endScaleValue = {0.0f, 0.0f, 0.0f}; // 終了時スケール値
    // ---- 回転 ----
    uint32_t enableRandomRotation = 0;        // 1=発生時にランダム初期角度
    Vector3 rotationMin = {0.0f, 0.0f, 0.0f}; // 初期角度 最小 (ラジアン, XYZ)
    Vector3 rotationMax = {0.0f, 0.0f, 0.0f}; // 初期角度 最大 (ラジアン, XYZ)
    float paddingRotMax{};
    uint32_t enableRandomAngularVelocity = 0;        // 1=発生時にランダム角速度
    Vector3 angularVelocityMin = {0.0f, 0.0f, 0.0f}; // 角速度 最小 (ラジアン/秒, XYZ)
    float paddingAngVelMin{};
    Vector3 angularVelocityMax = {0.0f, 0.0f, 0.0f}; // 角速度 最大 (ラジアン/秒, XYZ)
    // ---- 中間カラー (3-stop gradient) ----
    // HLSL packing: enableMidColor(4)+midColorRatio(4)+padMidColor(8) = 16bytes, then float4 midColor = 16bytes
    uint32_t enableMidColor = 0; // 1=有効: start→mid→end の3段階補間
    float midColorRatio = 0.5f;  // midColor に達するlife比率 [0,1]
    float padMidColor0 = 0.0f;
    float padMidColor1 = 0.0f;
    Vector4 midColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 中間色
    // ---- タービュランス ----
    uint32_t enableTurbulence = 0;    // 1=有効: per-particleランダム振動力
    float turbulenceStrength = 1.0f;  // 振動力の大きさ
    float turbulenceFrequency = 2.0f; // 振動周波数 (Hz)
    float turbulencePad = 0.0f;
    // ---- 発生形状 ----
    uint32_t emitShape = 0;        // 0=Box, 1=Sphere Surface, 2=Cone
    float emitSphereRadius = 1.0f; // Sphere/Cone 半径
    float emitConeAngle = 0.5236f; // Cone 半開角 (ラジアン, デフォルト30°)
    float emitShapePad = 0.0f;
    // ---- カラーグラデーション (N段・LUT を CB に同梱) ----
    // enableColorGradient=1 のとき、Update は colorLUT[lifeRatio*255] を色に使う（start/mid/end/random を上書き）。
    // colorLUT は CPU が colorStops からベイクした 256段 RGBA8(uint)。HLSL 側は uint4 colorLUT[64]（同じ 1024B）。
    // ※ HLSL CB は uint4 配列で16B境界に並ぶため、C++ uint32_t[256](1024B) と uint4[64](1024B) はバイト一致する。
    uint32_t enableColorGradient = 0;
    float colorGradPad0 = 0.0f;
    float colorGradPad1 = 0.0f;
    float colorGradPad2 = 0.0f;
    uint32_t colorLUT[256] = {};
    // ---- 寿命カーブ(サイズ/アルファ倍率)・LUT を CB に同梱 ----
    // enable*Curve=1 のとき Update が *CurveLUT[lifeRatio*255] を scale / color.a に乗算する。
    // CPU が CurvePoint 列からベイクした 256段 float。HLSL 側は float4[64]（同じ 1024B）。
    uint32_t enableSizeCurve = 0;
    uint32_t enableAlphaCurve = 0;
    float lifeCurvePad0 = 0.0f;
    float lifeCurvePad1 = 0.0f;
    float sizeCurveLUT[256] = {};
    float alphaCurveLUT[256] = {};
    // ---- 音声振動（音の“立ち上がり”に合わせてバンっと揺らす。形状を選ばない） ----
    // enableAudioVibration=1 のとき、Update が各粒子を「自分固有のランダム方向」へ
    //   高周波 sin 振動 × エンベロープ で揺らす（中心・半径を使わないのでどんな形でも使える）。
    //   ★エンベロープは CPU が「音の立ち上がり(onset)」で跳ね上げ、時間で指数減衰させる値。
    //     → 波形が大きくなった“瞬間”にバンっと強く震え、その後スッと落ち着く（＝振動っぽい）。
    //   ★sin は反転するので velocity は発散せず（＝飛んでいかない）。
    //   方向・位相・周波数を粒子ごとに散らすので動きがバラバラ＆ビートで一斉でなくズレて動く。
    // ※ HLSL ParticleCSSettings 末尾と一致させること（末尾追記なので既存オフセット不変＝OFFで無回帰）。
    uint32_t enableAudioVibration = 0;
    float audioVibrationStrength = 12.0f;   // 振動の大きさ（揺れ幅）
    float audioVibrationSensitivity = 4.0f; // 感度（音の立ち上がりに掛ける入力ゲイン。onset は音量より小さいので既定は大きめ）
    float audioAmplitude = 0.0f;            // 立ち上がりエンベロープ[0,1]（CPU が毎フレーム注入。振動の大きさを駆動）
    float audioVibrationFrequency = 22.0f;  // 振動の速さ（Hz的スケール。大きいほど細かく震える）
    float audioAttackSharpness = 1.8f;      // 反応カーブ指数（>1で大きい音だけドンと反応）
    float audioReleaseRate = 10.0f;         // エンベロープ減衰速度[1/s]（大きいほど早く落ち着く）
    float audioPad0 = 0.0f;                 // 16B境界パディング

    // ---- GPU駆動の視錐台カリング ----
    // Compute(Emit/Update) が「カメラの視錐台に入っているか」を粒子ごとに判定し、
    // 入っているものだけを描画リスト(gRenderCompact)へ詰める。画面外の粒子は
    // ExecuteIndirect の instanceCount に載らない＝頂点シェーダすら起動しない。
    // ※ シミュレーション（生存リスト）からは外さないので、画面外で時間が止まることはない。
    // ※ 判定は PerView.viewProjection から抽出した平面で行うので、描画に使う行列と完全に一致する。
    // HLSL ParticleCSSettings 末尾と一致させること。
    uint32_t enableFrustumCull = 1;      // 1=視錐台カリング（プレビュー描画中は CPU が 0 を入れる）
    float frustumRadiusScale = 1.5f;     // 粒子半径 = max(scale)×この係数（モデル頂点の広がり）
    float frustumStretchFactor = 0.0f;   // 速度ストレッチ係数（0=無効。半径を速度で膨らませる）
    float frustumPad0 = 0.0f;
    // ax+by+cz+d=0 の (a,b,c,d)。法線は視錐台の内側向き（内側で dot(n,p)+d が正）。
    // 並びは left/right/bottom/top/near/far。
    Vector4 frustumPlanes[6] = {};

    // ---- フリップブック（スプライトシート）----
    // 1枚のテクスチャに格子状に並べたコマを順に切り替えて「絵として動く」エフェクトを作る。
    // 炎・爆発・稲妻のような**形のあるもの**は、点を大量に飛ばすより1枚の絵を動かす方が似る。
    // コマ番号は DrawCore の scaleZ の**空いていた上位16bit**へ詰めるので、1粒あたりの帯域は増えない。
    // HLSL ParticleCSSettings（Particle.hlsli）の frustumPlanes の直後と一致させること
    uint32_t enableFlipbook = 0;
    uint32_t flipbookCols = 1;        // 横のコマ数
    uint32_t flipbookRows = 1;        // 縦のコマ数
    uint32_t flipbookMode = 0;        // 0=寿命で1周 / 1=fps でループ
    float flipbookFps = 30.0f;        // mode=1 のときのコマ送り速度
    uint32_t flipbookRandomStart = 0; // 1=粒ごとに開始コマをずらす
    float flipbookPad0 = 0.0f;
    float flipbookPad1 = 0.0f;

    // ---- プロシージャル形状（画像を用意せずに「形のある絵」を作る）----
    // 丸いソフトスプライトでは格闘ゲームの炎・稲妻にならないが、
    // スプライトシートを描き起こすのも大変なので、**形をシェーダーで計算して作る**。
    // 値はここで持ち、描画に要るぶんだけ毎フレーム マテリアルCB へ写す
    uint32_t shapeMode = 0;       // 0=テクスチャそのまま / 1=炎 / 2=シャード(稲妻) / 3=リング
    float shapeEdge = 0.5f;       // 輪郭のしきい値
    float shapeNoiseScale = 3.0f; // ゆらぎの細かさ
    float shapeRimWidth = 0.22f;  // 縁取りの太さ
    Vector4 shapeRimColor = {0.45f, 0.85f, 1.0f, 1.0f};
    float shapeSpeed = 1.6f;      // ゆらぎが動く速さ
    float shapeSoftness = 0.06f;  // 輪郭のぼかし（小さいほどセル調）
    // ---- ソフトパーティクル ----
    // 板が地面や壁へ刺さったときに出る「切り口の直線」を消す。
    // 背景との距離が近いほどアルファを落として、境目を溶かす
    uint32_t enableSoftParticle = 0;
    float softParticleFade = 1.0f; // この距離（ワールド単位）まで近づくと完全に消える
    // ---- UVスクロール ----
    // テクスチャ／プロシージャル形状のゆらぎを流す。炎が「立ち上る」動きになる
    Vector2 uvScrollSpeed = {0.0f, 0.0f};
    // ---- 縁の発光（フレネル）----
    // メッシュの輪郭（視線と面が平行なところ）ほど濃く、カメラ正面を向いた面ほど薄くする。
    // 体を包む殻に貼ると、中のキャラが透けて輪郭だけが光る「オーラ」の見え方になる。
    // 面の向きが要るので、ビルボードを切ったメッシュにだけ効く
    float shapeFresnel = 0.0f;
    float shapePad1 = 0.0f;

    // ---- 演出の基準空間（ここから下は CPU 専用。HLSL 側には対応メンバが無い）----
    // 「渦の回転軸」「渦/集束の目標オフセット」をどの空間の値として解釈するか。
    // GPU へ渡るのは解決済みのワールド値（vortexAxis / vortexTarget / gatherTarget）だけなので、
    // 以下のメンバは CB 末尾に乗るだけでシェーダからは一切参照されない
    //   （＝ HLSL struct ParticleCSSettings は audioPad0 までで一致していれば良い）。
    //
    //   0 = ワールド固定（従来動作。既存 Json は 0 になるので無回帰）
    //   1 = エミッター基準（エミッターの回転に追従。エミッター側をビルボードすればカメラにも追従する）
    //   2 = ビルボード（カメラの向きに追従＝カメラをどこへ回しても渦を正面から見た動きになる）
    uint32_t effectSpace = 0;
    Vector3 vortexAxisBase = {0.0f, 1.0f, 0.0f}; // 基準空間での回転軸（ImGui/Json が読み書きする元の値）
};
// HLSL 側 ParticleCSSettings（Particle.hlsli）とのオフセット契約。
// dxc の -Fc が出す cbuffer レイアウトと一致していること（ずれると視錐台カリングが誤動作する）。
static_assert(offsetof(ParticleCSSettings, enableFrustumCull) == 3680, "enableFrustumCull のオフセットずれ。HLSL ParticleCSSettings と要整合");
static_assert(offsetof(ParticleCSSettings, frustumPlanes) == 3696, "frustumPlanes のオフセットずれ。HLSL ParticleCSSettings と要整合");

/// =====================================================================
/// フィールドがパーティクルに適用する「一度きりの設定上書き」データ
/// overrideMask のビット（FieldOverrideBits）が立っている項目だけ上書きされる。
/// パーティクル側の settingsOverrideFlags に同じビットが既に立っていたら
/// 上書きをスキップし、一度きり保証を実現する。
///
/// 粒子単体へ確実に適用できる8項目のみ（旧45項目のうち大半は
/// グループCB設定のため粒子単体では適用不可能で、実際は効いていなかった）。
/// =====================================================================
struct ParticleFieldSettingsOverride
{
    /// 上書きするかどうかのビットマスク（0=上書きしない）
    /// FieldOverrideBits の組み合わせ
    uint32_t overrideMask = 0;

    // ---------- 上書き値 ----------
    // overrideMask の対応ビットが立っているときのみ使用される

    float lifeTimeMin = 0.5f;                          // 寿命上書き Min（Min/Max乱数）
    float lifeTimeMax = 1.0f;                          // 寿命上書き Max
    float scaleMin = 0.5f;                             // スケール上書き Min（Min/Max乱数）
    float scaleMax = 1.5f;                             // スケール上書き Max
    Vector3 velocityMin = {-0.5f, -0.5f, -0.5f};       // 速度置換 Min（成分ごと乱数）
    Vector3 velocityMax = {0.5f, 0.5f, 0.5f};          // 速度置換 Max
    float velocityMultiplier = 1.0f;                   // 速度倍率（一度だけ乗算）
    Vector3 accelImpulse = {0.0f, 0.0f, 0.0f};         // 加速度インパルス（一度だけ加算）
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};          // 色上書き（RGBを以後固定。Aは通常フェード継続）
    float trailSpawnDistance = 0.1f;                   // トレイル生成間隔の上書き
    Vector3 gatherTarget = {0.0f, 0.0f, 0.0f};         // 向け替えターゲット座標
};

// フィールド本体（形と効果）は gpu/ParticleCSField.h

/// =======================

/// ====== CPUParticle ======

/// <summary>
/// CPUパーティクルの挙動設定（寿命・速度・色・軌跡・各種オプションなど）
/// </summary>
struct ParticleSetting
{
    int maxTrailParticles; // 最大軌跡パーティクル数
    float gatherStartRatio = 0.5f;
    float gatherStrength = 2.0f;
    float trailSpawnInterval; // 軌跡パーティクル生成間隔
    float trailLifeScale{};   // 軌跡パーティクルの寿命スケール
    float lifeTimeMin{};
    float lifeTimeMax{};
    float gravity{};
    float alphaMin{};
    float alphaMax{};
    float scaleMin{};
    float scaleMax{};
    float trailVelocityScale{}; // 軌跡の速度スケール
    Vector3 translate{};
    Vector3 rotation{};
    Vector3 scale{};
    Vector3 velocityMin{};
    Vector3 velocityMax{};
    Vector3 particleStartScale{};
    Vector3 particleEndScale{};
    Vector3 startAcce{};
    Vector3 endAcce{};
    Vector3 startRotate{};
    Vector3 endRotate{};
    Vector3 rotateVelocityMin{};
    Vector3 rotateVelocityMax{};
    Vector3 allScaleMax{};
    Vector3 allScaleMin{};
    Vector3 rotateStartMax{};
    Vector3 rotateStartMin{};
    Vector3 trailScaleMultiplier{}; // 軌跡パーティクルのサイズ倍率
    Vector4 startColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 endColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 trailColorMultiplier{}; // 軌跡パーティクルの色倍率
    uint32_t count{};
    bool enableTrail{};          // 軌跡機能を有効にするか
    bool trailInheritVelocity{}; // 軌跡が親の速度を継承するか
    bool isRandomColor{};
    bool isBillboard = false;
    bool isBillboardX = false;
    bool isBillboardY = false;
    bool isBillboardZ = false;
    bool isRandomRotate = false;
    bool isRotateVelocity = false;
    bool isAcceMultiply = false;
    bool isRandomSize = false;
    bool isRandomAllSize = false;
    bool isSinMove = false;
    bool isFaceDirection = false;
    bool isEndScale = false;
    bool isEmitOnEdge = false;
    bool isGatherMode = false;

    BlendMode blendMode = BlendMode::Add;

    ParticleSetting() : enableTrail(false), trailSpawnInterval(0.05f),
                        maxTrailParticles(1), trailLifeScale(0.5f),
                        trailScaleMultiplier({0.8f, 0.8f, 0.8f}),
                        trailColorMultiplier({1.0f, 1.0f, 1.0f, 0.7f}),
                        trailInheritVelocity(true), trailVelocityScale(0.3f) {}
};

/// <summary>
/// パーティクルの統計情報（数・インスタンス数）
/// </summary>
struct ParticleStats
{
    size_t count = 0;
    size_t instanceCount = 0; // 同じ名前のエミッター数
};

/// <summary>
/// GPUへ送る描画用パーティクルデータ（WVP・World・色）
/// </summary>
struct ParticleForGPU
{
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

/// <summary>
/// CPUで管理する1パーティクル分のデータ
/// </summary>
struct Particle
{
    WorldTransform transform{}; // 位置
    Vector3 emitterPosition{};
    Vector3 velocity{}; // 速度
    Vector3 Acce{};
    Vector3 startScale{};
    Vector3 endScale{};
    Vector3 startAcce{};
    Vector3 endAcce{};
    Vector3 startRotate{};
    Vector3 endRotate{};
    Vector3 rotateVelocity{};
    Vector3 fixedDirection{};
    Vector4 color{};     // 色
    float lifeTime{};    // ライフタイム
    float currentTime{}; // 現在の時間
    float initialAlpha{};
    Vector3 relativePosition{}; // 親からの相対位置
    Vector3 parentOffset{};     // 親に対するオフセット
    bool isChild{};             // 子パーティクルかどうか
    bool createTrail{};         // 軌跡を作成するか
    float trailSpawnTimer{};    // 軌跡生成のタイマー
    float trailSpawnInterval{}; // 軌跡生成間隔
    int maxChildren{};          // 最大子供数
    float childLifeScale{};     // 子の寿命スケール（親より短く）

    BlendMode blendMode = BlendMode::Add;

    Particle() : isChild(false), createTrail(false), trailSpawnTimer(0.0f),
                 trailSpawnInterval(0.1f), maxChildren(10), childLifeScale(0.8f) {}
};

/// <summary>
/// CPUパーティクルグループの保持データ（マテリアル・パーティクル一覧・インスタンシング情報）
/// </summary>
struct ParticleGroupData
{
    // マテリアルデータ
    std::vector<ParticleMaterial> materials;
    // パーティクルのリスト (std::list<Particle> 型)
    std::list<Particle> particles;
    // インスタンシングデータ用SRVインデックス
    uint32_t instancingSRVIndex = 0;
    // インスタンシングリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource = nullptr;
    // インスタンス数
    uint32_t instanceCount = 0;
    // インスタンシングデータを書き込むためのポインタ
    ParticleForGPU *instancingData = nullptr;
    // グループ名
    std::string groupName;
    // ブレンドモード
    BlendMode blendMode = BlendMode::Add;
};

/// =========================

/// <summary>
/// MaterialData を ParticleMaterial へ変換
/// </summary>
/// <param name="material">変換元のマテリアルデータ</param>
/// <returns>ParticleMaterial: 変換後のパーティクルマテリアル</returns>
ParticleMaterial ForParticleMaterial(MaterialData material);

/// <summary>
/// MaterialData の配列を ParticleMaterial の配列へ変換
/// </summary>
/// <param name="materials">変換元のマテリアルデータ配列</param>
/// <returns>std::vector&lt;ParticleMaterial&gt;: 変換後の配列</returns>
std::vector<ParticleMaterial> ForParticleMaterials(std::vector<MaterialData> materials);
} // namespace Hagine
