
// 前方描画（定数バッファ経由）のライト枠。
// ディファードON時はポイントライトを StructuredBuffer から読むためこの枠を使わない。
// ※ LightGroup.h の MAX_POINT_LIGHTS / MAX_SPOT_LIGHTS と必ず一致させること。
#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS 32

struct VertexShaderOutput
{
    float4 position      : SV_POSITION;
    float2 texcoord      : TEXCOORD0;
    float3 normal        : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 shadowCoord   : POSITION1;
    // インスタンシング描画で「1つのマテリアル定数バッファを共有したまま個体ごとに色を変える」ための倍率。
    // 通常描画（非インスタンシング）の VS は白(1,1,1,1)を入れるので従来と結果は変わらない。
    // ※ この構造体を出力する全ての VS（Object3d / Skinning / Instanced）で必ず書き込むこと。
    float4 instanceColor : COLOR0;
};

struct ShadowData
{
    int enabled;
    float bias;
    float strength;
    float normalBias; // 面が光に対して浅いときに足すバイアス（縞状の影の対策）

    float softness;   // 影の縁のぼかし幅（シャドウマップのテクセル単位）
    int sampleCount;  // ぼかしのサンプル数
    float mapSize;    // シャドウマップの解像度
    float padding;
};

struct Material
{
    float4 color;
    int enableLighting;
    float3 padding; // パディングを明示的に追加
    float4x4 uvTransform;
    float shininess;
    float environmentCoefficient;
    int enableNormalMap;        // テクスチャ法線マップ有効
    int enableProceduralNormal; // 手続き的法線有効
    float normalStrength;       // 法線の強さ
    float proceduralScale;      // 手続きノイズのスケール
    int enableToon;             // このマテリアルにトゥーンを適用してよいか（実際に効くかは全体設定と併せて判定）
    float emissiveStrength;     // 自己発光の強さ。0で発光なし。発光色はアルベドを流用する
    float cameraFade;           // カメラに近いときの見え具合（1=普通 / 0=完全に消える）。CPU が距離から決める
    float3 materialPad;
};

// =============================================
// カメラに近い物の「網目抜き」（スクリーンドア）。
//   半透明にすると描画順・ディファードが破綻するので、不透明のまま 4x4 の網目で画素を間引いて透けて見せる。
//   fade が 1 未満のときだけ使う。閾値は画面の画素位置で決まるので、止まっていてもちらつかない。
// =============================================
bool CameraFadeDiscard(float fade, float2 pixelPosition)
{
    static const float kBayer[16] =
    {
        0.0f / 16.0f, 8.0f / 16.0f, 2.0f / 16.0f, 10.0f / 16.0f,
        12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f, 6.0f / 16.0f,
        3.0f / 16.0f, 11.0f / 16.0f, 1.0f / 16.0f, 9.0f / 16.0f,
        15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 5.0f / 16.0f,
    };
    const uint2 p = uint2(pixelPosition) & 3u;
    return fade < 1.0f && fade <= kBayer[p.y * 4u + p.x];
}

struct DirectionalLight
{
    float4 color; //<! ライトの色
    float3 direction; //!< ライトの向き
    float intensity; //!< 輝度
    int active;
    int HalfLambert;
    int BlinnPhong;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

struct Camera
{
    float3 worldPosition;
};

// ポイントライトの構造体
struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    int active;
    float radius;
    float decay;
    int HalfLambert;
    int BlinnPhong;
    float3 padding;
};

struct SpotLight
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction;
    float distance;
    float decay;
    float cosAngle;
    int active;
    int HalfLambert;
    int BlinnPhong;
    float3 padding;
};

struct PointLights
{
    PointLight lights[MAX_POINT_LIGHTS];
    int count;
    float3 padding;
};

struct SpotLights
{
    SpotLight lights[MAX_SPOT_LIGHTS];
    int count;
    float3 padding;
};