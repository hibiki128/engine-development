// カメラビュー窓の仕上げ。HDR（リニア）の絵を、メインの画面と同じトーンマップで 0〜1 へ収める。
// 設定（露出・曲線・コントラスト・彩度）は ToneMap.PS.hlsl と同じ定数バッファを使うので、見た目がそろう。

Texture2D<float4> gInput : register(t0);
RWTexture2D<float4> gOutput : register(u0);

struct ToneMapParameters
{
    float exposure;
    int mode; // 0=なし 1=Reinhard 2=ACES 3=Uncharted2
    float contrast;
    float saturation;

    float whitePoint;
    float3 colorFilter;
};
ConstantBuffer<ToneMapParameters> gToneMap : register(b0);

float3 ACESFilmic(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 Uncharted2Curve(float3 x)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    gOutput.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }

    float3 color = gInput[id.xy].rgb;
    color *= max(gToneMap.exposure, 0.0f);
    color *= gToneMap.colorFilter;

    const float white = max(gToneMap.whitePoint, 0.01f);
    if (gToneMap.mode == 1)
    {
        const float3 scaled = color * (1.0f + color / (white * white));
        color = scaled / (1.0f + color);
    }
    else if (gToneMap.mode == 2)
    {
        color = ACESFilmic(color);
    }
    else if (gToneMap.mode == 3)
    {
        const float3 curved = Uncharted2Curve(color * 2.0f);
        const float3 whiteScale = 1.0f / Uncharted2Curve(white.xxx);
        color = saturate(curved * whiteScale);
    }
    else
    {
        color = saturate(color);
    }

    const float luminance = Luminance(color);
    color = lerp(luminance.xxx, color, max(gToneMap.saturation, 0.0f));
    color = saturate((color - 0.5f) * max(gToneMap.contrast, 0.0f) + 0.5f);

    gOutput[id.xy] = float4(color, 1.0f);
}
