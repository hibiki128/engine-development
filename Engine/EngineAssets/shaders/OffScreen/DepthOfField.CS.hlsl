// =============================================================
// 被写界深度（Depth of Field）
//
// ピント面から外れているほど大きくボカす。
// ボケの形は円形（ディスク状）のサンプル配置で近似している。
//
// 前景（ピント面より手前）のボケが背景へにじみ出す問題を避けるため、
// 「自分よりボケ半径が小さい＝ピントが合っている画素」は寄与を弱めている。
// =============================================================

cbuffer DofParams : register(b0)
{
    float4x4 gProjectionInverse; // 射影行列の逆行列（NDC深度 → ビュー空間Z）
    float gFocusDistance;        // ピントの合う距離
    float gFocusRange;           // ピントが合っているとみなす前後の幅
    float gMaxBlurRadius;        // 最大のボケ半径（ピクセル）
    float gFalloff;              // ピント面から外れたときのボケの立ち上がり方
    int2 gTextureSize;           // 処理対象の解像度
    float2 gPadding;
};

Texture2D<float4> gSource : register(t0);
Texture2D<float> gDepth : register(t1);
RWTexture2D<float4> gOutput : register(u0);

// 円形に配置したサンプル点（半径1の円内に均等に散らしたもの）。
// 黄金角による螺旋配置なので、少ないサンプル数でも偏りが出にくい。
static const int kSampleCount = 24;
static const float2 kDiskSamples[kSampleCount] =
{
    float2(0.0000f, 0.0000f), float2(-0.1531f, 0.1214f), float2(0.0405f, -0.2739f),
    float2(0.1958f, 0.2896f), float2(-0.3969f, -0.0648f), float2(0.4187f, -0.2262f),
    float2(-0.1801f, 0.4900f), float2(-0.2100f, -0.5136f), float2(0.5443f, 0.2554f),
    float2(-0.6118f, 0.1653f), float2(0.3255f, -0.5719f), float2(0.1136f, 0.6763f),
    float2(-0.5460f, -0.4665f), float2(0.7267f, -0.0642f), float2(-0.4832f, 0.6017f),
    float2(-0.0432f, -0.7969f), float2(0.5620f, 0.5966f), float2(-0.8140f, -0.1215f),
    float2(0.6162f, -0.5748f), float2(-0.1092f, 0.8580f), float2(-0.4855f, -0.7375f),
    float2(0.8593f, 0.2549f), float2(-0.7674f, 0.4879f), float2(0.2543f, -0.8862f),
};

/// NDCの深度からビュー空間の直線的な深度（カメラからの距離）を求める
float LinearizeDepth(float ndcDepth)
{
    float4 viewSpace = mul(float4(0.0f, 0.0f, ndcDepth, 1.0f), gProjectionInverse);
    return viewSpace.z / max(viewSpace.w, 1e-6f);
}

/// その画素のボケ半径（Circle of Confusion）をピクセル単位で求める
float CalcBlurRadius(float linearDepth)
{
    // ピント面からの距離。gFocusRange のぶんは完全にピントが合っているとみなす。
    float distanceFromFocus = abs(linearDepth - gFocusDistance);
    float outOfFocus = max(0.0f, distanceFromFocus - gFocusRange * 0.5f);

    // ピント面から離れるほどボケを強くする。gFalloff が小さいほど急激にボケる。
    float normalized = saturate(outOfFocus / max(gFalloff, 1e-4f));
    return normalized * gMaxBlurRadius;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gTextureSize.x || pixel.y >= gTextureSize.y)
    {
        return;
    }

    float4 centerColor = gSource.Load(int3(pixel, 0));
    float centerRawDepth = gDepth.Load(int3(pixel, 0));

    // 空など深度が書かれていない場所は最遠として扱う
    float centerDepth = (centerRawDepth >= 1.0f) ? 1e6f : LinearizeDepth(centerRawDepth);
    float centerRadius = CalcBlurRadius(centerDepth);

    // ボケ半径が1ピクセル未満ならそのまま出す（ピントが合っている領域を無駄にぼかさない）
    if (centerRadius < 1.0f)
    {
        gOutput[pixel] = centerColor;
        return;
    }

    float3 accumulated = centerColor.rgb;
    float totalWeight = 1.0f;

    [unroll]
    for (int i = 1; i < kSampleCount; ++i)
    {
        int2 samplePixel = clamp(pixel + int2(kDiskSamples[i] * centerRadius),
                                 int2(0, 0), gTextureSize - int2(1, 1));

        float sampleRawDepth = gDepth.Load(int3(samplePixel, 0));
        float sampleDepth = (sampleRawDepth >= 1.0f) ? 1e6f : LinearizeDepth(sampleRawDepth);
        float sampleRadius = CalcBlurRadius(sampleDepth);

        // ピントが合っている画素（ボケ半径が小さい画素）を、
        // ボケている画素へ大きく混ぜると輪郭がにじむ。半径の比で寄与を抑える。
        float weight = saturate(sampleRadius / centerRadius);

        accumulated += gSource.Load(int3(samplePixel, 0)).rgb * weight;
        totalWeight += weight;
    }

    gOutput[pixel] = float4(accumulated / totalWeight, centerColor.a);
}
