
#define MAX_POINT_LIGHTS 5
#define MAX_SPOT_LIGHTS 5

struct VertexShaderOutput
{
    float4 position      : SV_POSITION;
    float2 texcoord      : TEXCOORD0;
    float3 normal        : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 shadowCoord   : POSITION1;
};

struct ShadowData
{
    int enabled;
    float bias;
    float strength;
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
    float2 padding2; // 16バイト境界に合わせるためのパディング
};

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