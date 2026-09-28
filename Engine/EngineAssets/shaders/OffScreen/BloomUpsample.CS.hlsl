// =============================================================
// ブルーム 3/4: 拡大＋加算（アップサンプル）
//
// 小さいミップを1段上へ広げ、その段に既に入っている絵へ**足し込む**。
// これを一番下から順に繰り返すと、広い滲みと細かい滲みが重なって
// 「近くは締まっていて、遠くへ向かってなだらかに広がる」自然な形になる。
// 段ごとに単純なガウスを掛けて足すやり方より、段の継ぎ目が出にくい。
//
// 拡大には9タップのテントフィルタを使う。バイリニア1回で拡大すると
// 四角いブロックの継ぎ目が見えるので、その面積を広げてなまして消す。
// gFilterRadius は画面の縦横比に依らないよう UV 空間で指定する。
// =============================================================

cbuffer BloomUpsampleParams : register(b0)
{
    int2 gDstSize;       // 出力（大きい側）の寸法
    int2 gSrcSize;       // 入力（小さい側）の寸法
    float gFilterRadius; // テントフィルタの広がり（UV空間）
    float gBlend;        // 足し込む強さ（1.0 でそのまま加算）
    float2 gPad0;
};

Texture2D<float4> gSource : register(t0);   // 1段下（小さい）のミップ
RWTexture2D<float4> gOutput : register(u0); // この段（大きい）。既存の値へ加算する
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
    float dx = gFilterRadius;
    float dy = gFilterRadius;

    // 3x3 テント（中央4 / 辺2 / 角1、合計16で割る）
    float3 a = gSource.SampleLevel(gSampler, uv + float2(-dx,  dy), 0).rgb;
    float3 b = gSource.SampleLevel(gSampler, uv + float2(  0,  dy), 0).rgb;
    float3 c = gSource.SampleLevel(gSampler, uv + float2( dx,  dy), 0).rgb;
    float3 d = gSource.SampleLevel(gSampler, uv + float2(-dx,   0), 0).rgb;
    float3 e = gSource.SampleLevel(gSampler, uv, 0).rgb;
    float3 f = gSource.SampleLevel(gSampler, uv + float2( dx,   0), 0).rgb;
    float3 g = gSource.SampleLevel(gSampler, uv + float2(-dx, -dy), 0).rgb;
    float3 h = gSource.SampleLevel(gSampler, uv + float2(  0, -dy), 0).rgb;
    float3 i = gSource.SampleLevel(gSampler, uv + float2( dx, -dy), 0).rgb;

    float3 color = e * 4.0f;
    color += (b + d + f + h) * 2.0f;
    color += (a + c + g + i);
    color *= (1.0f / 16.0f);

    // この段に既にある値へ足す（アップサンプルの積み上げ）
    float3 current = gOutput[pixel].rgb;
    gOutput[pixel] = float4(current + color * gBlend, 1.0f);
}
