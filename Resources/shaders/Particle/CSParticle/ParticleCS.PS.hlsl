#include "../Particle.hlsli"

struct Material
{
    float4 color;
    float4x4 uvTransform;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

ConstantBuffer<Material> gMaterial : register(b1);
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);


PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    // CS パーティクルの uvTransform は常に単位行列のため、per-pixel の 4x4 変換を省略する。
    // （オーバードロー時はこの 1 画素あたりの行列積が積み上がるため、削るとフィルが軽くなる）
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    output.color = gMaterial.color * textureColor * input.color;
    // 完全に寄与しない画素のみ discard（加算合成では微小寄与の総和が見えるため閾値は上げない）。
    if (output.color.a == 0.0f)
    {
        discard;
    }
    return output;
}
