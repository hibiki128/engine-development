struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    // 粒ごとに固定の種。プロシージャル形状のゆらぎを粒ごとにずらすのに使う
    // （フリップブック有効時はコマ番号が入る）。CPUパーティクル側は 0 を入れる
    float seed : TEXCOORD1;
    // 面がカメラを向いている度合い |dot(法線, 視線)|。1=正面 / 0=輪郭。
    // 縁の発光（shapeFresnel）に使う。ビルボードやCPUパーティクルは 1 を入れる
    float facing : TEXCOORD2;
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
// DrawCore の scaleZ: 下位16bit=half(z) / **上位16bit=フリップブックのコマ番号**。
// 空いていた上位16bitを使うので、フリップブックを足しても1粒あたりの帯域は増えない
uint PackScaleZFrame(float3 s, uint frame)
{
    return f32tof16(s.z) | ((frame & 0xFFFFu) << 16);
}
uint UnpackFlipbookFrame(uint z)
{
    return z >> 16;
}
/// フリップブックのコマ番号を決める。
///   mode 0: 寿命をコマ数で割って1周（爆発・着弾のように「1回再生して終わり」）
///   mode 1: fps でループ（炎・オーラのように「ずっと動き続ける」）
/// randomStart=1 なら粒ごとに開始コマをずらして、全部が同じ絵にならないようにする
uint ComputeFlipbookFrame(uint cols, uint rows, uint mode, float fps, uint randomStart,
                          float lifeRatio, float time, uint particleIndex)
{
    uint frameCount = max(cols * rows, 1u);
    uint startOffset = 0u;
    if (randomStart != 0u)
    {
        startOffset = (uint) (frac(sin(float(particleIndex) * 91.37f) * 43758.5453f) * float(frameCount));
    }

    uint frame;
    if (mode == 0u)
    {
        // 寿命で1周。最後のコマで止まるよう frameCount-1 で丸める
        frame = (uint) (saturate(lifeRatio) * float(frameCount - 1u) + 0.5f);
    }
    else
    {
        frame = (uint) (max(time, 0.0f) * max(fps, 0.0f));
    }
    return (frame + startOffset) % frameCount;
}
/// DrawCore の scaleZ 上位16bit へ詰める値を決める。
///   フリップブック有効 → コマ番号
///   無効              → プロシージャル形状のゆらぎ用の「粒ごとに固定の種」
/// どちらも VS が同じ場所から読んで PS へ渡す
uint ComputeParticleWord(uint enableFlipbook, uint cols, uint rows, uint mode, float fps, uint randomStart,
                         float lifeRatio, float time, uint particleIndex)
{
    if (enableFlipbook != 0u)
    {
        return ComputeFlipbookFrame(cols, rows, mode, fps, randomStart, lifeRatio, time, particleIndex);
    }
    return (uint) (frac(sin(float(particleIndex) * 12.9898f) * 43758.5453f) * 65535.0f);
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

// =============================================
// GPU駆動の視錐台カリング
//   粒子を「中心＋半径の球」とみなして視錐台6平面と判定する。
//   平面は描画に使う viewProjection から抽出しているので、判定結果は
//   VS が実際に射影する結果と一致する（半径ぶんの余裕は radiusScale で持たせる）。
//   ビルボードでもワールド固定でも回転しても、球で包んでいる限り判定は保守的。
// =============================================
bool IsSphereInFrustum(float4 planes[6], float3 center, float radius)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        if (dot(planes[i].xyz, center) + planes[i].w < -radius)
            return false; // どれか1つの平面の外側なら画面に映らない
    }
    return true;
}

// 粒子の描画半径（球の半径）。スケールの最大成分にモデルの広がり係数を掛ける。
// 速度ストレッチ有効時は伸びるぶんを速度で膨らませる（stretchFactor=0 なら無影響）。
float ParticleCullRadius(float3 scale, float3 velocity, float radiusScale, float stretchFactor)
{
    float r = max(max(abs(scale.x), abs(scale.y)), abs(scale.z)) * radiusScale;
    if (stretchFactor > 0.0f)
    {
        r *= (1.0f + length(velocity) * stretchFactor);
    }
    return r;
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
    // ---- フリップブック（VSがUVをずらす）。C++ PerView と一致させること ----
    uint enableFlipbookDraw;
    uint flipbookColsDraw;
    uint flipbookRowsDraw;
    uint flipbookDrawPad;
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
    // 発生数へ掛ける係数[0,1]。発生の立ち上がり（スポーン率のフェードイン）。1.0 で従来どおり
    float emitRateScale;
};

struct PerFrame
{
    float time;
    float deltaTime;
    int groupId;
    // このグループが受けるフィールドの番号をビットで（レイヤーの一致判定は CPU で済ませてある）
    uint fieldUpdateMask; // 粒子を動かす・変える効果を持つフィールド
    uint fieldEmitMask;   // 今フレーム「この範囲から発生」するフィールド
    uint perFramePad0;
    uint perFramePad1;
    uint perFramePad2;
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
    // ---- GPU駆動の視錐台カリング。C++ ParticleCSSettings と一致させること ----
    uint enableFrustumCull;      // 1=視錐台の外の粒子を描画リストに載せない（シミュは継続）
    float frustumRadiusScale;    // 粒子半径 = max(scale)×この係数
    float frustumStretchFactor;  // 速度ストレッチ係数（0=無効）
    float frustumPad0;
    float4 frustumPlanes[6];     // left/right/bottom/top/near/far（内側で dot(n,p)+d が正）
    // ---- フリップブック（スプライトシート）----
    // 1枚のテクスチャに並べたコマを順に切り替えて絵として動かす。
    // 「たくさんの点を飛ばす」のではなく「形のある絵を動かす」ための土台
    uint enableFlipbook;
    uint flipbookCols;        // 横のコマ数
    uint flipbookRows;        // 縦のコマ数
    uint flipbookMode;        // 0=寿命で1周 / 1=fps でループ
    float flipbookFps;        // mode=1 のときのコマ送り速度
    uint flipbookRandomStart; // 1=粒ごとに開始コマをずらす
    float flipbookPad0;
    float flipbookPad1;
    // ---- プロシージャル形状（画像を使わずPSで形を作る）----
    uint shapeMode;
    float shapeEdge;
    float shapeNoiseScale;
    float shapeRimWidth;
    float4 shapeRimColor;
    float shapeSpeed;
    float shapeSoftness;
    // ---- ソフトパーティクル（背景に近いほど薄くして刺さった断面を消す）----
    uint enableSoftParticle;
    float softParticleFade;
    // ---- UVスクロール ----
    float2 uvScrollSpeed;
    float shapeFresnel;     // 縁の発光（描画はマテリアルCB経由。ここでは参照しない）
    float shapePad1;
    // ※ C++ ParticleCSSettings はこの後ろに CPU 専用メンバ（effectSpace / vortexAxisBase）を持つ。
    //   それらは CB 末尾に乗るだけでシェーダからは参照しない（渦の軸・目標は CPU 側で
    //   ワールド空間へ解決してから vortexAxis / vortexTarget / gatherTarget に入れて渡す）。
    //   ここまでのレイアウトが一致していれば良い。
};

// 【重要】このレイアウトは C++ 側 `struct ParticleFieldGPU`
//   （Engine/3d/particle/gpu/ParticleCSField.h）と**バイト単位で一致**させること（合計160バイト）。
//   C++ 側には sizeof/offsetof の static_assert があり、ずれるとビルドで検出される。
struct ParticleFieldGPU
{
    // 形（ワールド→ローカルの軸3本。回転行列を組まずに内積だけで済ませる）
    float3 center;
    uint shape;           // 0=球 1=箱 2=円柱
    float3 axisX;
    float boundRadiusSq;  // 形を包む球の半径²
    float3 axisY;
    float falloffStart;   // 芯の大きさ（0〜1）
    float3 axisZ;
    uint falloffCurve;    // 0=一定 1=直線 2=なめらか
    float3 halfExtent;    // 球: x=半径 / 箱: 各軸の半分 / 円柱: x=半径 y=高さの半分
    uint effectFlags;     // FE_*

    // 力
    float3 windVelocity;
    float windResponse;
    float vortexSpeed;
    float vortexResponse;
    float attractStrength;
    float absorbRadius;
    float dragPerSecond;
    float lifeSpeed;
    float sizeScale;
    float trailSpawnDistance;

    // 見た目
    float4 tint;

    // 発生（Emit 用）
    float emitLifeMin;
    float emitLifeMax;
    uint emitCount;
    uint fieldPad0;
};

// 効果のビット。C++ 側 FieldEffectBits（ParticleCSField.h）と一致させること。
static const uint FE_Wind = 1u << 0;
static const uint FE_Attract = 1u << 1;
static const uint FE_Vortex = 1u << 2;
static const uint FE_Drag = 1u << 3;
static const uint FE_Tint = 1u << 4;
static const uint FE_Size = 1u << 5;
static const uint FE_Life = 1u << 6;
static const uint FE_Kill = 1u << 7;
static const uint FE_Trail = 1u << 8;
static const uint FE_Once = 1u << 9;

// =============================================
// フィールドの範囲判定と影響度。
//   戻り値: 範囲内なら true。outInfluence に 0〜1（芯の内側=1、端=0 に向かって弱まる）
//   outLocal: フィールドのローカル座標（渦の軸まわりの計算に使う）
// =============================================
bool EvaluateField(ParticleFieldGPU f, float3 worldPos, out float outInfluence, out float3 outLocal)
{
    outInfluence = 0.0f;
    outLocal = float3(0.0f, 0.0f, 0.0f);
    float3 d = worldPos - f.center;
    // 形を包む球の外なら即座に外（ほとんどの粒子はここで抜ける）
    if (dot(d, d) >= f.boundRadiusSq)
        return false;

    outLocal = float3(dot(d, f.axisX), dot(d, f.axisY), dot(d, f.axisZ));
    // 中心=0、境界=1 の正規化距離
    float n;
    if (f.shape == 0u)
    {
        n = length(outLocal) / f.halfExtent.x;
    }
    else if (f.shape == 1u)
    {
        float3 q = abs(outLocal) / f.halfExtent;
        n = max(q.x, max(q.y, q.z));
    }
    else
    {
        n = max(length(outLocal.xz) / f.halfExtent.x, abs(outLocal.y) / f.halfExtent.y);
    }
    if (n >= 1.0f)
        return false;

    float t = saturate((n - f.falloffStart) / max(1.0f - f.falloffStart, 1e-4f));
    if (f.falloffCurve == 0u)
        outInfluence = 1.0f;
    else if (f.falloffCurve == 1u)
        outInfluence = 1.0f - t;
    else
        outInfluence = 1.0f - smoothstep(0.0f, 1.0f, t);
    return true;
}

// 一度きり設定上書きのビット定数。
// C++ 側 FieldOverrideBits（ParticleCSFieldSettingOverride.h）と一致させること。
// パーティクル側の settingsOverrideFlags.x に同じビットを記録して一度きり保証する。
static const uint OB_LifeTime = 0u;       // 寿命を Min/Max 乱数で上書き
static const uint OB_Scale = 1u;          // スケールを Min/Max 乱数で上書き
static const uint OB_Velocity = 2u;       // 速度を Min/Max 乱数で置換
static const uint OB_VelocityMul = 3u;    // 速度に倍率を一度だけ乗算
static const uint OB_AccelImpulse = 4u;   // 速度に加速度を一度だけ加算
static const uint OB_Color = 5u;          // 色(RGB)を上書き（以後この色で固定）
static const uint OB_TrailDistance = 6u;  // トレイル生成間隔を上書き
static const uint OB_GatherRedirect = 7u; // 速度をターゲット方向へ向け替え

// 【重要】このレイアウトは C++ 側 GPU_FieldSettingsOverride
//   （ParticleCSFieldManager.cpp）と**バイト単位で一致**させること（合計112バイト）。
struct ParticleFieldSettingsOverrideData
{
    uint overrideMask;        // FieldOverrideBits の組み合わせ
    float lifeTimeMin;
    float lifeTimeMax;
    float scaleMin;
    float scaleMax;
    float velocityMultiplier;
    float trailSpawnDistance;
    float pad0;
    float3 velocityMin;
    float pad1;
    float3 velocityMax;
    float pad2;
    float3 accelImpulse;
    float pad3;
    float4 color;
    float3 gatherTarget;
    float pad4;
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