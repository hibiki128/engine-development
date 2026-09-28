// =============================================================
// ブルーム 1/4: 抽出（プリフィルタ）
//
// シーンから「光らせる成分」を取り出しながら半分の解像度へ縮める。
//
// 旧 Bloom.CS.hlsl との違い:
//   ・しきい値が**ソフトニー**。旧実装は `輝度 > しきい値 ? 色 : 0` の
//     ハードカットで、明るさがしきい値をまたぐ画素がフレームごとに
//     点いたり消えたりしてチラついていた。ここでは境界をなめらかに繋ぐ。
//   ・13タップの縮小フィルタ ＋ **Karis 平均**。ごく一部だけ極端に明るい画素
//     （蛍のような点）があると、ぼかした後に大きな四角い滲みになる。
//     4画素ずつの部分平均を輝度で重み付けしてから混ぜることでこれを抑える。
//
// パーティクルだけを光らせるモード:
//   gParticleOnly=1 のとき、t0(パーティクル描画後) から t1(描画前) を引いた差、
//   つまり**パーティクルがそのフレームで足した色だけ**を対象にする。
//   加算ブレンドの粒子はそのまま足し算なので、この差が寄与そのものになる。
// =============================================================

cbuffer BloomPrefilterParams : register(b0)
{
    float gThreshold;    // ここから光り始める輝度
    float gKnee;         // しきい値まわりのなめらかさ（0 でハードカット）
    uint gParticleOnly;  // 1 = パーティクルの寄与だけを抽出する
    uint gPad0;
    int2 gDstSize;       // 出力（半解像度）の寸法
    int2 gSrcSize;       // 入力（全解像度）の寸法
};

Texture2D<float4> gScene : register(t0);       // パーティクル描画後
Texture2D<float4> gSceneNoParticle : register(t1); // パーティクル描画前
RWTexture2D<float4> gOutput : register(u0);
SamplerState gSampler : register(s0);

float Luminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

/// そのUVで「光らせたい色」を取る
float3 SampleSource(float2 uv)
{
    float3 color = gScene.SampleLevel(gSampler, uv, 0).rgb;
    if (gParticleOnly != 0)
    {
        // パーティクルが足したぶんだけ。背景より暗くなる画素は 0 に落とす
        float3 before = gSceneNoParticle.SampleLevel(gSampler, uv, 0).rgb;
        color = max(color - before, 0.0f);
    }
    return color;
}

/// ソフトニーのしきい値処理（Unity/Unreal と同じ形）
float3 Prefilter(float3 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    // knee の幅で二次曲線に繋ぐ。しきい値の手前から緩やかに立ち上がる
    float knee = max(gKnee, 1e-4f);
    float soft = brightness - gThreshold + knee;
    soft = clamp(soft, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + 1e-4f);
    float contribution = max(soft, brightness - gThreshold) / max(brightness, 1e-4f);
    return color * contribution;
}

/// Karis 平均: 明るすぎる点1つに引きずられないよう、輝度の逆数で重み付けして混ぜる
float3 KarisAverage(float3 a, float3 b, float3 c, float3 d)
{
    float wa = 1.0f / (1.0f + Luminance(a));
    float wb = 1.0f / (1.0f + Luminance(b));
    float wc = 1.0f / (1.0f + Luminance(c));
    float wd = 1.0f / (1.0f + Luminance(d));
    float sum = wa + wb + wc + wd;
    return (a * wa + b * wb + c * wc + d * wd) / max(sum, 1e-4f);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gDstSize.x || pixel.y >= gDstSize.y)
    {
        return;
    }

    // 出力画素の中心に対応する入力UV
    float2 uv = (float2(pixel) + 0.5f) / float2(gDstSize);
    float2 texel = 1.0f / float2(gSrcSize);

    // 13タップ（COD:AW の縮小フィルタ）。中央の 2x2 と、その外側の 3x3
    float3 a = SampleSource(uv + texel * float2(-2, -2));
    float3 b = SampleSource(uv + texel * float2( 0, -2));
    float3 c = SampleSource(uv + texel * float2( 2, -2));
    float3 d = SampleSource(uv + texel * float2(-2,  0));
    float3 e = SampleSource(uv);
    float3 f = SampleSource(uv + texel * float2( 2,  0));
    float3 g = SampleSource(uv + texel * float2(-2,  2));
    float3 h = SampleSource(uv + texel * float2( 0,  2));
    float3 i = SampleSource(uv + texel * float2( 2,  2));
    float3 j = SampleSource(uv + texel * float2(-1, -1));
    float3 k = SampleSource(uv + texel * float2( 1, -1));
    float3 l = SampleSource(uv + texel * float2(-1,  1));
    float3 m = SampleSource(uv + texel * float2( 1,  1));

    // 5つの 2x2 ブロックへ分けてから Karis 平均を掛ける（点光源のちらつき対策）
    float3 group0 = KarisAverage(a, b, d, e) * 0.125f;
    float3 group1 = KarisAverage(b, c, e, f) * 0.125f;
    float3 group2 = KarisAverage(d, e, g, h) * 0.125f;
    float3 group3 = KarisAverage(e, f, h, i) * 0.125f;
    float3 group4 = KarisAverage(j, k, l, m) * 0.5f;

    float3 color = group0 + group1 + group2 + group3 + group4;

    gOutput[pixel] = float4(Prefilter(color), 1.0f);
}
