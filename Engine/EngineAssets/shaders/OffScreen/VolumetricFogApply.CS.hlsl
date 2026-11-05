// =============================================================
// ボリュメトリックフォグ: 合成
//
// 画素の奥行きに対応するスライスで積分結果を引き、
// 色 = 元の色 × 透過率 + 散乱した光 としてシーンへ書き戻す。
// =============================================================

cbuffer ApplyParams : register(b0)
{
    float gDepthScale;   // 射影行列の _43
    float gDepthOffset;  // 射影行列の _33
    float gNear;         // 一番手前のスライスの奥行き
    float gFar;          // 一番奥のスライスの奥行き
    int2 gTextureSize;   // 画面の解像度
    int gGridDepth;      // 奥行き方向の分割数
    float gSkyStrength;  // 空に掛ける割合
};

Texture3D<float4> gIntegrated : register(t0);
Texture2D<float> gDepth : register(t1);
RWTexture2D<float4> gScene : register(u0);

SamplerState gSamplerLinear : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (any(pixel >= gTextureSize))
    {
        return;
    }

    float rawDepth = gDepth.Load(int3(pixel, 0));
    bool isSky = (rawDepth >= 1.0f);

    // 深度バッファの値からビュー空間の奥行きへ
    float viewDepth = isSky ? gFar : gDepthScale / (rawDepth - gDepthOffset);
    float slice = saturate(log(max(viewDepth, gNear) / gNear) / log(gFar / gNear));

    // スライス z には奥の境界までの結果が入っているので、半セル手前を引く。
    // 一番手前のスライスより近い所は、霧なしから最初のスライスへつなぐ
    float2 uv = (float2(pixel) + 0.5f) / float2(gTextureSize);
    float sliceCells = slice * float(gGridDepth);
    float4 fog;
    if (sliceCells < 1.0f)
    {
        float4 firstSlice = gIntegrated.SampleLevel(gSamplerLinear, float3(uv, 0.5f / float(gGridDepth)), 0);
        fog = lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), firstSlice, sliceCells);
    }
    else
    {
        fog = gIntegrated.SampleLevel(gSamplerLinear, float3(uv, (sliceCells - 0.5f) / float(gGridDepth)), 0);
    }

    if (isSky)
    {
        fog = lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), fog, gSkyStrength);
    }

    float4 color = gScene[pixel];
    gScene[pixel] = float4(color.rgb * fog.a + fog.rgb, color.a);
}
