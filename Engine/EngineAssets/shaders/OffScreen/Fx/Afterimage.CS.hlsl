// 残像（モーショントレイル）。前のフレームの結果を少しずつ残して重ねる。
// 「明るい方を残す」にすると、光る物の軌跡だけが尾を引く
#include "PostFxCommon.hlsli"

cbuffer AfterimageParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gPersistence;  // 残る割合（大きいほど長く残る）
    int gMode;           // 0=混ぜる / 1=明るい方を残す（光の軌跡）
    float gTintStrength; // 残像に色を付ける量
    int gReset;          // 1なら前のフレームを捨てて今の画だけにする
    float3 gTint;        // 残像の色
    float gPadding;
};

// 前のフレームでこのエフェクトが出した結果
Texture2D<float4> gHistory : register(t1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    if (gReset != 0)
    {
        gOutput[pixel] = source;
        return;
    }

    float4 history = gHistory.Load(int3(pixel, 0));
    float3 trail = history.rgb * lerp(float3(1.0f, 1.0f, 1.0f), gTint, gTintStrength);
    float3 color;
    if (gMode == 0)
    {
        color = lerp(source.rgb, trail, saturate(gPersistence));
    }
    else
    {
        color = max(source.rgb, trail * saturate(gPersistence));
    }
    gOutput[pixel] = float4(color, source.a);
}
