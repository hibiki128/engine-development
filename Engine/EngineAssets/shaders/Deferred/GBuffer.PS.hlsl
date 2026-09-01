#include "../Object/Object3d.hlsli"
#include "Deferred.hlsli"

// ============================================================
// G-Buffer 書き込みパス
//   Object3d.VS / Skinning.VS のどちらからも使える（頂点出力が同一のため）。
//   ライティングは行わず、DeferredLighting.PS が必要とする材料だけを書き出す。
//   法線マッピングと両面補正は「見た目を前方描画と一致させるため」ここで済ませる。
// ============================================================

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<Camera> gCamera : register(b2);
SamplerState gSampler : register(s0);
Texture2D<float4> gTexture : register(t0);
Texture2D<float4> gNormalMap : register(t3);

struct GBufferOutput
{
    float4 albedo : SV_TARGET0;   // rgb=アルベド, a=アルファ
    float4 normal : SV_TARGET1;   // xyz=ワールド法線, w=光沢度
    float4 material : SV_TARGET2; // r=環境係数/4, g=ライティング有効
};

// 画面空間微分からコタンジェントフレーム(TBN)を作る（Object3d.PS と同一）
float3x3 CotangentFrame(float3 N, float3 worldPos, float2 uv)
{
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = rsqrt(max(dot(T, T), dot(B, B)));
    return float3x3(T * invmax, B * invmax, N);
}

float3 PerturbNormal(float3 N, float3 worldPos, float2 uv, float3 tangentNormal)
{
    float3x3 TBN = CotangentFrame(N, worldPos, uv);
    return normalize(mul(tangentNormal, TBN));
}

// --- 手続き的法線用の簡易値ノイズ（Object3d.PS と同一）---
float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i + float2(0.0f, 0.0f));
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float HeightField(float2 p)
{
    float h = 0.0f;
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        h += ValueNoise(p) * amp;
        p *= 2.0f;
        amp *= 0.5f;
    }
    return h;
}

float3 ProceduralTangentNormal(float2 worldXZ, float scale, float strength)
{
    float2 p = worldXZ * scale;
    float eps = 0.5f;
    float h = HeightField(p);
    float hx = HeightField(p + float2(eps, 0.0f));
    float hy = HeightField(p + float2(0.0f, eps));
    float dhdx = (hx - h) / eps;
    float dhdy = (hy - h) / eps;
    return normalize(float3(-dhdx * strength, -dhdy * strength, 1.0f));
}

GBufferOutput main(VertexShaderOutput input)
{
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    // インスタンシング描画の個体色を畳み込む（通常描画は白なので従来と同じ）
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy) * input.instanceColor;

    // 前方描画と同じく、アルファが完全に0の画素は捨てる（深度も書かない）
    float4 baseColor = gMaterial.color * textureColor;
    if (textureColor.a == 0.0f || baseColor.a == 0.0f)
    {
        discard;
    }

    // ── 法線マッピング（幾何法線を摂動）──────────────────
    float3 normal = input.normal;
    if (gMaterial.enableProceduralNormal != 0)
    {
        float3 geomN = normalize(normal);
        float3 tN = ProceduralTangentNormal(input.worldPosition.xz, gMaterial.proceduralScale, gMaterial.normalStrength);
        normal = PerturbNormal(geomN, input.worldPosition, input.worldPosition.xz * gMaterial.proceduralScale, tN);
    }
    else if (gMaterial.enableNormalMap != 0)
    {
        float3 geomN = normalize(normal);
        float3 tN = gNormalMap.Sample(gSampler, transformedUV.xy).xyz * 2.0f - 1.0f;
        tN.xy *= gMaterial.normalStrength;
        tN = normalize(tN);
        normal = PerturbNormal(geomN, input.worldPosition, transformedUV.xy, tN);
    }

    // 両面ライティング: 視線と逆を向く面は法線を反転しておく。
    // ライティングパスでは視線ベクトルを再計算できるが、前方描画と完全に同じ
    // 結果にするためここで確定させる。
    if (gMaterial.enableLighting != 0)
    {
        float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
        if (dot(normalize(normal), viewDir) < 0.0f)
        {
            normal = -normal;
        }
    }

    GBufferOutput output;
    output.albedo = float4(baseColor.rgb, baseColor.a);
    output.normal = float4(normalize(normal), gMaterial.shininess);
    output.material = float4(
        saturate(gMaterial.environmentCoefficient / DEFERRED_ENV_COEFF_RANGE),
        gMaterial.enableLighting != 0 ? 1.0f : 0.0f,
        0.0f,
        1.0f);
    return output;
}
