// =============================================================
// ブルーム（分離2パス版・コンピュートシェーダー）
//
// 旧ピクセルシェーダー版は 5x5 = 25 タップを1パスで回し、
// そのすべてで輝度判定（明るい部分の抽出）を行っていた。
//
// ここでは
//   ・1パス目: 明るい部分を抜き出しながら「横方向」にぼかす
//   ・2パス目: それを「縦方向」にぼかし、元画像へ加算する
// と分けている。タップ数は 25 → 5+5=10 になり、輝度判定は1パス目だけで済む。
//
// カーネルはガウス（二項係数）なので分離できる。
// =============================================================

cbuffer BloomParams : register(b0)
{
    float gThreshold;  // この輝度を超えた部分だけ光らせる
    float gIntensity;  // 加算するブルームの強さ
    int gDirection;    // 0 = 横方向（明るい部分の抽出も行う）, 1 = 縦方向（元画像へ加算）
    int gPadding;
    int2 gTextureSize; // 処理対象の解像度
    int2 gPadding2;
};

// そのパスへの入力（1パス目=元画像, 2パス目=1パス目の結果）
Texture2D<float4> gSource : register(t0);
// エフェクトへの入力そのもの（2パス目で元画像へ加算するために使う）
Texture2D<float4> gOriginal : register(t1);
RWTexture2D<float4> gOutput : register(u0);

// 5タップのガウス係数（二項係数 1,4,6,4,1 を 16 で割ったもの）
static const float kWeights[5] = {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};

/// BT.709 の輝度
float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

/// しきい値を超えた明るい成分だけを取り出す
float3 ExtractBright(float3 color)
{
    return (Luminance(color) > gThreshold) ? color : float3(0.0f, 0.0f, 0.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gTextureSize.x || pixel.y >= gTextureSize.y)
    {
        return;
    }

    int2 step = (gDirection == 0) ? int2(1, 0) : int2(0, 1);

    float3 blurred = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = -2; i <= 2; ++i)
    {
        int2 samplePixel = clamp(pixel + step * i, int2(0, 0), gTextureSize - int2(1, 1));
        float3 sampled = gSource.Load(int3(samplePixel, 0)).rgb;

        // 明るい部分の抽出は1パス目だけ。2パス目の入力はすでに抽出済みなのでそのまま使う。
        if (gDirection == 0)
        {
            sampled = ExtractBright(sampled);
        }
        blurred += sampled * kWeights[i + 2];
    }

    if (gDirection == 0)
    {
        // 1パス目は「横方向にぼかした明るい成分」をそのまま中間結果として出す
        gOutput[pixel] = float4(blurred, 1.0f);
    }
    else
    {
        // 2パス目で元画像へ加算する
        float4 originalColor = gOriginal.Load(int3(pixel, 0));
        gOutput[pixel] = float4(originalColor.rgb + blurred * gIntensity, originalColor.a);
    }
}
