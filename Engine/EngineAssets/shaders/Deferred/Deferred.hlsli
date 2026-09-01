#ifndef HAGINE_DEFERRED_HLSLI
#define HAGINE_DEFERRED_HLSLI

// ============================================================
// ディファードレンダリング共通定義
//   G-Buffer の内訳:
//     GB0 : R8G8B8A8_UNORM_SRGB    rgb = アルベド(material.color * texture), a = アルファ
//     GB1 : R16G16B16A16_FLOAT     xyz = ワールド法線(摂動・両面補正済み), w = 光沢度
//     GB2 : R8G8B8A8_UNORM         r   = 環境マップ係数 / 4, g = ライティング有効フラグ
//     深度: 既存の深度バッファ（ワールド座標はここから復元する）
// ============================================================

// ライトカリングのタイルサイズ（ピクセル）。CS のスレッドグループサイズと必ず一致させること
#define DEFERRED_TILE_SIZE 16
// 1タイルが保持できるポイントライトの最大数
#define DEFERRED_MAX_LIGHTS_PER_TILE 256
// タイルごとのライトインデックス配列の要素数（先頭1個が件数）
#define DEFERRED_TILE_STRIDE (DEFERRED_MAX_LIGHTS_PER_TILE + 1)

// 環境マップ係数を8bitへ詰めるときのレンジ（0〜4を想定）
#define DEFERRED_ENV_COEFF_RANGE 4.0f
// スポットライトの最大数。※ LightGroup.h の MAX_SPOT_LIGHTS と一致させること
// （スポットは定数バッファ経由でタイルカリングの対象外なので、点光源ほどは増やせない）
#define MAX_SPOT_LIGHTS_DEFERRED 32
// 光沢度はRGBA16Fにそのまま入れるので変換不要

/// <summary>
/// ポイントライト（StructuredBuffer要素）。C++ の LightGroup::PointLightGPU と一致させること
/// </summary>
struct PointLightGPU
{
    float3 position; // ワールド座標
    float radius;    // 影響半径
    float3 color;    // 色
    float intensity; // 輝度
    float decay;     // 減衰率
    uint flags;      // bit0: HalfLambert / bit1: BlinnPhong
    float2 padding;
};

#define POINT_LIGHT_FLAG_HALF_LAMBERT 1u
#define POINT_LIGHT_FLAG_BLINN_PHONG 2u

/// <summary>
/// ディファードのパス間で共有する定数
/// </summary>
struct DeferredConstants
{
    matrix invViewProjection;   // クリップ→ワールド復元用
    matrix view;                // ライトカリングのビュー空間変換用
    matrix projection;          // タイル錐台の構築用
    matrix lightViewProjection; // シャドウマップ用（ワールド→ライトクリップ）
    float3 cameraPosition;      // カメラのワールド座標
    uint pointLightCount;       // CPUが積んだポイントライト数（粒子光源は含まない）
    uint2 screenSize;           // 描画解像度（ピクセル）
    uint2 tileCount;            // タイル数（X, Y）
    float nearZ;                // near クリップ
    float farZ;                 // far クリップ
    uint pointLightCapacity;    // ライトバッファの容量（GPU生成分の溢れを切り捨てる上限）
    float padding;
};

/// <summary>
/// 深度バッファ値とスクリーンUVからワールド座標を復元する
/// </summary>
/// <param name="uv">0〜1のスクリーンUV（左上原点）</param>
/// <param name="deviceDepth">深度バッファの値（0〜1）</param>
/// <param name="invViewProj">ビュープロジェクションの逆行列</param>
float3 ReconstructWorldPosition(float2 uv, float deviceDepth, matrix invViewProj)
{
    // UV(左上原点) → NDC
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 clip = float4(ndc, deviceDepth, 1.0f);
    // 行ベクトル規約（-Zpr）なのでベクトルを左から掛ける
    float4 world = mul(clip, invViewProj);
    return world.xyz / world.w;
}

#endif // HAGINE_DEFERRED_HLSLI
