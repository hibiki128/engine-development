#include"object3d.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLights> gPointLights : register(b3);
ConstantBuffer<SpotLights> gSpotLights : register(b4);
ConstantBuffer<ShadowData> gShadowData : register(b5);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);
Texture2D<float4> gTexture : register(t0);
TextureCube<float4> gEngironmentTexture : register(t1);
Texture2D<float> gShadowMap : register(t2);

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

PixelShaderOutput main(VertexShaderOutput input)
{
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    PixelShaderOutput output;

    // シャドウ係数の計算
    float shadowFactor = 1.0f;
    if (gShadowData.enabled != 0)
    {
        float3 projCoord = input.shadowCoord.xyz / input.shadowCoord.w;
        float2 shadowUV = projCoord.xy * float2(0.5f, -0.5f) + 0.5f;

        // シャドウマップの範囲内かチェック
        if (shadowUV.x >= 0.0f && shadowUV.x <= 1.0f &&
            shadowUV.y >= 0.0f && shadowUV.y <= 1.0f &&
            projCoord.z >= 0.0f && projCoord.z <= 1.0f)
        {
            float shadowDepth = projCoord.z - gShadowData.bias;
            float shadow = SampleShadowPCF(shadowUV, shadowDepth);
            shadowFactor = lerp(1.0f - gShadowData.strength, 1.0f, shadow);
        }
    }

    if (gMaterial.enableLighting != 0)
    {
        // 両面ライティング: 法線が視線と逆を向く面（薄い板の裏面や、法線が下を向いた床など）は
        // 法線を反転し、どちらの面から見ても正しく陰影が付くようにする。
        // 閉じたモデルの可視面は元から視線側を向くため影響せず、裏面はカリングされるため見た目は変わらない。
        float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
        if (dot(normalize(input.normal), viewDir) < 0.0f)
        {
            input.normal = -input.normal;
        }

        output.color.rgb = float3(0.0f, 0.0f, 0.0f);

        // 指向性ライトの計算
        if (gDirectionalLight.active != 0)
        {
            if (gDirectionalLight.HalfLambert != 0)
            {
                float NdotL = dot(normalize(input.normal), normalize(-gDirectionalLight.direction));
                float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
                output.color = gMaterial.color * textureColor * gDirectionalLight.color * cos * gDirectionalLight.intensity;
                output.color.rgb *= shadowFactor;
            }
            else if (gDirectionalLight.BlinnPhong != 0)
            {
                float NdotLDirectional = dot(normalize(input.normal), normalize(-gDirectionalLight.direction));
                float cosDirectional = pow(NdotLDirectional * 0.5f + 0.5f, 2.0f);
                float3 toEyeDirectional = normalize(gCamera.worldPosition - input.worldPosition);
                float3 halfVectorDirectional = normalize(-gDirectionalLight.direction + toEyeDirectional);
                float NDotHDirectional = dot(normalize(input.normal), halfVectorDirectional);
                float specularPowDirectional = pow(saturate(NDotHDirectional), gMaterial.shininess);

                float3 diffuseDirectional = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cosDirectional * gDirectionalLight.intensity;
                float3 specularDirectional = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPowDirectional * float3(1.0f, 1.0f, 1.0f);
                output.color.rgb += (diffuseDirectional + specularDirectional) * shadowFactor;
            }
        }

        // 複数のポイントライト計算
        for (int i = 0; i < min(gPointLights.count, MAX_POINT_LIGHTS); i++)
        {
            PointLight pointLight = gPointLights.lights[i];

            if (pointLight.active != 0)
            {
                if (pointLight.HalfLambert != 0)
                {
                    float3 lightDir = pointLight.position - input.worldPosition;
                    float distance = length(lightDir);
                    lightDir = normalize(lightDir);

                    float NdotL = dot(normalize(input.normal), lightDir);
                    float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);

                    float factor = pow(saturate(-distance / pointLight.radius + 1.0f), pointLight.decay);

                    output.color.rgb += gMaterial.color.rgb * textureColor.rgb * pointLight.color.rgb * cos * pointLight.intensity * factor;
                }
                else if (pointLight.BlinnPhong != 0)
                {
                    float3 lightDir = pointLight.position - input.worldPosition;
                    float distance = length(lightDir);
                    lightDir = normalize(lightDir);

                    float NdotLPoint = dot(normalize(input.normal), lightDir);
                    float cosPoint = max(NdotLPoint, 0.0f);
                    float3 diffusePoint = gMaterial.color.rgb * textureColor.rgb * pointLight.color.rgb * cosPoint * pointLight.intensity;

                    float3 toEyePoint = normalize(gCamera.worldPosition - input.worldPosition);
                    float3 halfVectorPoint = normalize(lightDir + toEyePoint);
                    float NDotHPoint = dot(normalize(input.normal), halfVectorPoint);
                    float specularPowPoint = pow(saturate(NDotHPoint), gMaterial.shininess);
                    float3 specularPoint = pointLight.color.rgb * pointLight.intensity * specularPowPoint * float3(1.0f, 1.0f, 1.0f);

                    float factor = pow(saturate(-distance / pointLight.radius + 1.0f), pointLight.decay);

                    output.color.rgb += (diffusePoint + specularPoint) * factor;
                }
            }
        }

        // 複数のスポットライト計算
        for (int j = 0; j < min(gSpotLights.count, MAX_SPOT_LIGHTS); j++)
        {
            SpotLight spotLight = gSpotLights.lights[j];

            if (spotLight.active != 0)
            {
                if (spotLight.HalfLambert != 0)
                {
                    float3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLight.position);
                    float cosAngle = dot(spotLightDirectionOnSurface, spotLight.direction);

                    float falloffFactor = saturate((cosAngle - spotLight.cosAngle) / (1.0f - spotLight.cosAngle));

                    float distance = length(spotLight.position - input.worldPosition);
                    float attenuationFactor = pow(saturate(-distance / spotLight.distance + 1.0f), spotLight.decay);

                    float NdotL = max(dot(normalize(input.normal), -spotLightDirectionOnSurface), 0.0f);
                    float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);

                    output.color.rgb += gMaterial.color.rgb * textureColor.rgb * spotLight.color.rgb * spotLight.intensity * attenuationFactor * falloffFactor * cos;
                }

                if (spotLight.BlinnPhong != 0)
                {
                    float3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLight.position);
                    float cosAngle = dot(spotLightDirectionOnSurface, spotLight.direction);

                    float falloffFactor = saturate((cosAngle - spotLight.cosAngle) / (1.0f - spotLight.cosAngle));

                    float distance = length(spotLight.position - input.worldPosition);
                    float attenuationFactor = pow(saturate(-distance / spotLight.distance + 1.0f), spotLight.decay);

                    float NdotLSpot = max(dot(normalize(input.normal), -spotLightDirectionOnSurface), 0.0f);
                    float3 diffuseSpot = gMaterial.color.rgb * textureColor.rgb * spotLight.color.rgb * NdotLSpot * spotLight.intensity;

                    float3 toEyeSpot = normalize(gCamera.worldPosition - input.worldPosition);
                    float3 halfVectorSpot = normalize(-spotLightDirectionOnSurface + toEyeSpot);
                    float NDotHSpot = dot(normalize(input.normal), halfVectorSpot);
                    float specularFactor = pow(saturate(NDotHSpot), gMaterial.shininess);
                    float3 specularSpot = spotLight.color.rgb * spotLight.intensity * specularFactor * float3(1.0f, 1.0f, 1.0f);

                    float3 spotLightContribution = (diffuseSpot + specularSpot) * attenuationFactor * falloffFactor;

                    output.color.rgb += spotLightContribution;
                }
            }
        }

        // 環境マッピング
        float3 cameraToPosition = normalize(input.worldPosition - gCamera.worldPosition);
        float3 reflectedVector = reflect(cameraToPosition, normalize(input.normal));
        float4 environmentColor = gEngironmentTexture.Sample(gSampler, reflectedVector);

        output.color.rgb += environmentColor.rgb * gMaterial.environmentCoefficient;

        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        output.color = gMaterial.color * textureColor;
        output.color.rgb *= shadowFactor;
    }

    if (textureColor.a == 0.0f)
    {
        discard;
    }
    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}
