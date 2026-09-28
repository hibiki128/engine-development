// =============================================================
// ブルーム 4/4: 合成
//
// 積み上げ終わったミップ0（半解像度）を全解像度へ広げ、シーンへ加算する。
// トーンマップの**手前**で足すこと。HDR のまま足すので、
// 強い光ほど広く伸びて、最後のトーンマップが自然に白へ収束させてくれる。
// =============================================================

// ※ シーンは SRV では読まず、**UAV として読み書きする**。
//   同じリソースを SRV と UAV に同時に入れることはできない（状態が両立しない）ので、
//   `gOutput[pixel]` を読んで足して書き戻す形にしている。
//   各スレッドが自分の画素しか触らないので競合しない。

cbuffer BloomCompositeParams : register(b0)
{
    int2 gDstSize;      // 出力（全解像度）の寸法
    float gIntensity;   // ブルームの強さ
    float gPad0;
};

Texture2D<float4> gBloom : register(t0);    // 積み上げたブルーム（半解像度）
RWTexture2D<float4> gScene : register(u0);  // シーン。ここへ足し込む
SamplerState gSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gDstSize.x || pixel.y >= gDstSize.y)
    {
        return;
    }

    float2 uv = (float2(pixel) + 0.5f) / float2(gDstSize);

    float4 scene = gScene[pixel];
    float3 bloom = gBloom.SampleLevel(gSampler, uv, 0).rgb;

    gScene[pixel] = float4(scene.rgb + bloom * gIntensity, scene.a);
}
