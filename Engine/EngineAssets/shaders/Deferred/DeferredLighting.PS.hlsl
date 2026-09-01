#include "../OffScreen/FullScreen.hlsli"
#include "Deferred.hlsli"

// ============================================================
// ディファードのライティングパス
//   G-Buffer と深度から材質・法線・ワールド座標を取り出し、
//   タイルごとにカリング済みのポイントライトだけを適用する。
//   陰影の数式は Object3d.PS（前方描画）と一致させてあるので、
//   ディファードON/OFFで見た目が変わらない。
// ============================================================

struct DirectionalLightGPU
{
    float4 color;
    float3 direction;
    float intensity;
    int active;
    int HalfLambert;
    int BlinnPhong;
};

struct SpotLightGPU
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

struct SpotLightsGPU
{
    SpotLightGPU lights[MAX_SPOT_LIGHTS_DEFERRED];
    int count;
    float3 padding;
};

struct ShadowDataGPU
{
    int enabled;
    float bias;
    float strength;
    float padding;
};

ConstantBuffer<DeferredConstants> gConstants : register(b0);
ConstantBuffer<DirectionalLightGPU> gDirectionalLight : register(b1);
ConstantBuffer<SpotLightsGPU> gSpotLights : register(b2);
ConstantBuffer<ShadowDataGPU> gShadowData : register(b3);

Texture2D<float4> gAlbedo : register(t0);
Texture2D<float4> gNormal : register(t1);
Texture2D<float4> gMaterialTex : register(t2);
Texture2D<float> gDepth : register(t3);
Texture2D<float> gShadowMap : register(t4);
TextureCube<float4> gEnvironmentTexture : register(t5);
StructuredBuffer<PointLightGPU> gPointLights : register(t6);
StructuredBuffer<uint> gTileLightIndices : register(t7);

SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

float SampleShadowPCF(float2 shadowUV, float shadowDepth)
{
    float2 texelSize = float2(1.0f / 2048.0f, 1.0f / 2048.0f);
    float shadow = 0.0f;
    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler, shadowUV + float2(x, y) * texelSize, shadowDepth);
        }
    }
    return shadow / 9.0f;
}

float4 main(VertexShaderOutput input) : SV_TARGET
{
    const int2 pixel = int2(input.position.xy);
    const float deviceDepth = gDepth.Load(int3(pixel, 0));

    // 何も描かれていない画素はオフスクリーンのクリア色を残す
    // （このあとスカイボックスや半透明が前方描画で重なる）
    if (deviceDepth >= 1.0f)
    {
        discard;
    }

    const float4 albedoSample = gAlbedo.Load(int3(pixel, 0));
    const float4 normalSample = gNormal.Load(int3(pixel, 0));
    const float4 materialSample = gMaterialTex.Load(int3(pixel, 0));

    const float3 albedo = albedoSample.rgb;

    // このパスは不透明ジオメトリを合成した結果を書き出す（ブレンドは無効なので、
    // ここで出したアルファがそのままレンダーターゲットに残る）。
    // アルベドのアルファ（被弾点滅などでマテリアル側が下げている値）を書き出すと、
    // エディタでシーンを ImGui::Image 表示したときにクリア色が透けて見えるため、常に不透明にする
    const float outputAlpha = 1.0f;
    const float3 normal = normalize(normalSample.xyz);
    const float shininess = normalSample.w;
    const float environmentCoefficient = materialSample.r * DEFERRED_ENV_COEFF_RANGE;
    const bool enableLighting = materialSample.g > 0.5f;

    const float2 uv = (float2(pixel) + 0.5f) / float2(gConstants.screenSize);
    const float3 worldPosition = ReconstructWorldPosition(uv, deviceDepth, gConstants.invViewProjection);
    const float3 toEye = normalize(gConstants.cameraPosition - worldPosition);

    // ── シャドウ係数（前方描画と同じ計算。shadowCoord は worldPos × LightVP で再現できる）──
    float shadowFactor = 1.0f;
    if (gShadowData.enabled != 0)
    {
        float4 shadowCoord = mul(float4(worldPosition, 1.0f), gConstants.lightViewProjection);
        float3 projCoord = shadowCoord.xyz / shadowCoord.w;
        float2 shadowUV = projCoord.xy * float2(0.5f, -0.5f) + 0.5f;

        if (shadowUV.x >= 0.0f && shadowUV.x <= 1.0f &&
            shadowUV.y >= 0.0f && shadowUV.y <= 1.0f &&
            projCoord.z >= 0.0f && projCoord.z <= 1.0f)
        {
            float shadowDepth = projCoord.z - gShadowData.bias;
            float shadow = SampleShadowPCF(shadowUV, shadowDepth);
            shadowFactor = lerp(1.0f - gShadowData.strength, 1.0f, shadow);
        }
    }

    if (!enableLighting)
    {
        // ライティング無効のマテリアルはアルベドをそのまま出す（影だけ乗る）
        return float4(albedo * shadowFactor, outputAlpha);
    }

    float3 color = float3(0.0f, 0.0f, 0.0f);

    // ── 平行光源 ──
    if (gDirectionalLight.active != 0)
    {
        float3 lightDir = normalize(-gDirectionalLight.direction);
        if (gDirectionalLight.HalfLambert != 0)
        {
            float NdotL = dot(normal, lightDir);
            float cosHalf = pow(NdotL * 0.5f + 0.5f, 2.0f);
            color = albedo * gDirectionalLight.color.rgb * cosHalf * gDirectionalLight.intensity;
            color *= shadowFactor;
        }
        else if (gDirectionalLight.BlinnPhong != 0)
        {
            float NdotL = dot(normal, lightDir);
            float cosHalf = pow(NdotL * 0.5f + 0.5f, 2.0f);
            float3 halfVector = normalize(lightDir + toEye);
            float NDotH = dot(normal, halfVector);
            float specularPow = pow(saturate(NDotH), shininess);

            float3 diffuse = albedo * gDirectionalLight.color.rgb * cosHalf * gDirectionalLight.intensity;
            float3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow;
            color += (diffuse + specular) * shadowFactor;
        }
    }

    // ── ポイントライト（このタイルに残ったものだけ）──
    {
        const uint2 tile = uint2(pixel) / DEFERRED_TILE_SIZE;
        const uint tileIndex = tile.y * gConstants.tileCount.x + tile.x;
        const uint base = tileIndex * DEFERRED_TILE_STRIDE;
        const uint tileLightCount = gTileLightIndices[base];

        for (uint li = 0; li < tileLightCount; ++li)
        {
            const PointLightGPU light = gPointLights[gTileLightIndices[base + 1u + li]];

            float3 toLight = light.position - worldPosition;
            float distance = length(toLight);
            if (distance > light.radius)
            {
                continue;
            }
            float3 lightDir = toLight / max(distance, 1e-6f);
            float factor = pow(saturate(-distance / light.radius + 1.0f), light.decay);

            if (light.flags & POINT_LIGHT_FLAG_HALF_LAMBERT)
            {
                float NdotL = dot(normal, lightDir);
                float cosHalf = pow(NdotL * 0.5f + 0.5f, 2.0f);
                color += albedo * light.color * cosHalf * light.intensity * factor;
            }
            else if (light.flags & POINT_LIGHT_FLAG_BLINN_PHONG)
            {
                float NdotL = dot(normal, lightDir);
                float cosPoint = max(NdotL, 0.0f);
                float3 diffuse = albedo * light.color * cosPoint * light.intensity;

                float3 halfVector = normalize(lightDir + toEye);
                float NDotH = dot(normal, halfVector);
                float specularPow = pow(saturate(NDotH), shininess);
                float3 specular = light.color * light.intensity * specularPow;

                color += (diffuse + specular) * factor;
            }
        }
    }

    // ── スポットライト ──
    for (int j = 0; j < min(gSpotLights.count, MAX_SPOT_LIGHTS_DEFERRED); j++)
    {
        SpotLightGPU spotLight = gSpotLights.lights[j];
        if (spotLight.active == 0)
        {
            continue;
        }

        float3 spotDirectionOnSurface = normalize(worldPosition - spotLight.position);
        float cosAngle = dot(spotDirectionOnSurface, spotLight.direction);
        float falloffFactor = saturate((cosAngle - spotLight.cosAngle) / (1.0f - spotLight.cosAngle));

        float distance = length(spotLight.position - worldPosition);
        float attenuationFactor = pow(saturate(-distance / spotLight.distance + 1.0f), spotLight.decay);

        if (spotLight.HalfLambert != 0)
        {
            float NdotL = max(dot(normal, -spotDirectionOnSurface), 0.0f);
            float cosHalf = pow(NdotL * 0.5f + 0.5f, 2.0f);
            color += albedo * spotLight.color.rgb * spotLight.intensity * attenuationFactor * falloffFactor * cosHalf;
        }

        if (spotLight.BlinnPhong != 0)
        {
            float NdotL = max(dot(normal, -spotDirectionOnSurface), 0.0f);
            float3 diffuse = albedo * spotLight.color.rgb * NdotL * spotLight.intensity;

            float3 halfVector = normalize(-spotDirectionOnSurface + toEye);
            float NDotH = dot(normal, halfVector);
            float specularFactor = pow(saturate(NDotH), shininess);
            float3 specular = spotLight.color.rgb * spotLight.intensity * specularFactor;

            color += (diffuse + specular) * attenuationFactor * falloffFactor;
        }
    }

    // ── 環境マッピング ──
    float3 cameraToPosition = normalize(worldPosition - gConstants.cameraPosition);
    float3 reflectedVector = reflect(cameraToPosition, normal);
    float4 environmentColor = gEnvironmentTexture.Sample(gSampler, reflectedVector);
    color += environmentColor.rgb * environmentCoefficient;

    return float4(color, outputAlpha);
}
