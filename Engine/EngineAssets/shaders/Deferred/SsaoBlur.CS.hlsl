// SSAO のならし。
// 遮蔽の計算はランダムな方向へ飛ばすぶんザラつくので、
// 近所の値を平均してムラを消す。深度の差が大きい所はまたがないようにして、
// 物の輪郭がぼやけないようにする。

Texture2D<float> gAmbientOcclusion : register(t0);
Texture2D<float> gDepth : register(t1);
RWTexture2D<float> gOutput : register(u0);

struct SsaoBlurParameters
{
    uint2 screenSize;    // 描画解像度
    int radius;          // ならす範囲（ピクセル）
    float depthThreshold; // これ以上深度が違う相手は混ぜない
};
ConstantBuffer<SsaoBlurParameters> gBlur : register(b0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= gBlur.screenSize.x || pixel.y >= gBlur.screenSize.y)
    {
        return;
    }

    const float centerDepth = gDepth.Load(int3(pixel, 0));
    const int radius = clamp(gBlur.radius, 0, 4);

    float sum = 0.0f;
    float weightSum = 0.0f;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const int2 samplePixel = int2(pixel) + int2(x, y);
            if (samplePixel.x < 0 || samplePixel.y < 0 ||
                samplePixel.x >= (int)gBlur.screenSize.x || samplePixel.y >= (int)gBlur.screenSize.y)
            {
                continue;
            }

            // 奥行きが大きく違う相手を混ぜると、輪郭ににじみが出る
            const float sampleDepth = gDepth.Load(int3(samplePixel, 0));
            if (abs(sampleDepth - centerDepth) > gBlur.depthThreshold)
            {
                continue;
            }

            sum += gAmbientOcclusion.Load(int3(samplePixel, 0));
            weightSum += 1.0f;
        }
    }

    gOutput[pixel] = (weightSum > 0.0f) ? (sum / weightSum) : gAmbientOcclusion.Load(int3(pixel, 0));
}
