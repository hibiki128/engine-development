#include"Sprite.hlsli"

struct Material
{
    float4 color;
    float4x4 uvTransform;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t1);
SamplerState gSampler : register(s0);

PixelShaderOutput main(VertexShaderOutput input)
{
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    PixelShaderOutput output;

    // gMaterial.color は ImGui の ColorEdit4 等で指定された sRGB(ガンマ)空間の値。
    // テクスチャは _SRGB フォーマットでサンプル時にリニア化済みなので、
    // マテリアル色もリニアへ変換してから乗算する。
    // 変換しないと SRGB RTV への書き込みで二重にガンマがかかり、色が薄く（明るく）出る。
    // αは輝度ではないため変換しない
    float3 materialLinear = pow(max(gMaterial.color.rgb, 0.0f), 2.2f);
    output.color.rgb = materialLinear * textureColor.rgb;
    output.color.a = gMaterial.color.a * textureColor.a;

    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}
