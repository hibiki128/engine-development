// =============================================================
// ブルーム 2/4: 縮小（ダウンサンプル）
//
// ミップ列を1段ずつ小さくしていく。段を下るほど1タップが画面上の広い範囲を
// 覆うので、**同じ13タップのまま、にじみの半径だけが倍々に広がる**。
// 旧実装（全解像度で±2画素の5タップ）が「光の縁がうっすら滲む」程度だったのに対し、
// こちらは画面の何割にも及ぶ本来のブルームになる。
//
// 抽出（しきい値）はプリフィルタで済んでいるので、ここでは掛けない。
// Karis 平均も最初の1段だけでよいのでここでは使わない（かけ過ぎると眠くなる）。
// =============================================================

cbuffer BloomDownsampleParams : register(b0)
{
    int2 gDstSize; // 出力の寸法
    int2 gSrcSize; // 入力の寸法
};

Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);
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
    float2 texel = 1.0f / float2(gSrcSize);

    float3 a = gSource.SampleLevel(gSampler, uv + texel * float2(-2, -2), 0).rgb;
    float3 b = gSource.SampleLevel(gSampler, uv + texel * float2( 0, -2), 0).rgb;
    float3 c = gSource.SampleLevel(gSampler, uv + texel * float2( 2, -2), 0).rgb;
    float3 d = gSource.SampleLevel(gSampler, uv + texel * float2(-2,  0), 0).rgb;
    float3 e = gSource.SampleLevel(gSampler, uv, 0).rgb;
    float3 f = gSource.SampleLevel(gSampler, uv + texel * float2( 2,  0), 0).rgb;
    float3 g = gSource.SampleLevel(gSampler, uv + texel * float2(-2,  2), 0).rgb;
    float3 h = gSource.SampleLevel(gSampler, uv + texel * float2( 0,  2), 0).rgb;
    float3 i = gSource.SampleLevel(gSampler, uv + texel * float2( 2,  2), 0).rgb;
    float3 j = gSource.SampleLevel(gSampler, uv + texel * float2(-1, -1), 0).rgb;
    float3 k = gSource.SampleLevel(gSampler, uv + texel * float2( 1, -1), 0).rgb;
    float3 l = gSource.SampleLevel(gSampler, uv + texel * float2(-1,  1), 0).rgb;
    float3 m = gSource.SampleLevel(gSampler, uv + texel * float2( 1,  1), 0).rgb;

    // 中央の 2x2 を重く、外周を軽く。合計 1.0
    float3 color = e * 0.125f;
    color += (a + c + g + i) * 0.03125f;
    color += (b + d + f + h) * 0.0625f;
    color += (j + k + l + m) * 0.125f;

    gOutput[pixel] = float4(color, 1.0f);
}
