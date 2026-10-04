// ミラー（鏡像）。画面の半分を折り返して左右・上下・四方を対称にする
#include "PostFxCommon.hlsli"

cbuffer MirrorParams : register(b0)
{
    POSTFX_FRAME_HEADER
    int gMode;        // 0=左を右へ / 1=右を左へ / 2=上を下へ / 3=下を上へ / 4=四方（左上を写す）
    float gPosition;  // 折り返す位置（0〜1）
    float2 gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float axis = clamp(gPosition, 0.01f, 0.99f);
    if (gMode == 0 && uv.x > axis)
        uv.x = axis - (uv.x - axis);
    else if (gMode == 1 && uv.x < axis)
        uv.x = axis + (axis - uv.x);
    else if (gMode == 2 && uv.y > axis)
        uv.y = axis - (uv.y - axis);
    else if (gMode == 3 && uv.y < axis)
        uv.y = axis + (axis - uv.y);
    else if (gMode == 4)
    {
        if (uv.x > 0.5f)
            uv.x = 1.0f - uv.x;
        if (uv.y > 0.5f)
            uv.y = 1.0f - uv.y;
    }
    gOutput[pixel] = SampleSourceMirror(uv);
}
