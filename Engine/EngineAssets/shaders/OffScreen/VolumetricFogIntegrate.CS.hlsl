// =============================================================
// ボリュメトリックフォグ: 積分
//
// 格子の各列（画面上の1セル）を手前から奥へたどり、
// 散乱した光の累計と透過率をスライスごとに書く。
// スライス z には「スライス z の奥の境界まで」の結果が入る。
// =============================================================

cbuffer IntegrateParams : register(b0)
{
    float gProjectionScaleX; // 射影行列の _11
    float gProjectionScaleY; // 射影行列の _22
    float gNear;             // 一番手前のスライスの奥行き
    float gFar;              // 一番奥のスライスの奥行き
    int3 gGridSize;          // 格子の分割数
    float gPadding;
};

Texture3D<float4> gScatter : register(t0);
RWTexture3D<float4> gIntegrated : register(u0);

/// 格子の奥行き方向の位置（0〜1）からビュー空間の奥行きへ
float SliceToViewDepth(float slice)
{
    return gNear * pow(gFar / gNear, slice);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 column = int2(dispatchThreadID.xy);
    if (any(column >= gGridSize.xy))
    {
        return;
    }

    // ビュー空間の奥行き 1 あたりに視線が進む距離
    float2 uv = (float2(column) + 0.5f) / float2(gGridSize.xy);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float rayScale = length(float3(ndc.x / gProjectionScaleX, ndc.y / gProjectionScaleY, 1.0f));

    float3 accumulated = float3(0.0f, 0.0f, 0.0f);
    float transmittance = 1.0f;

    [loop]
    for (int z = 0; z < gGridSize.z; ++z)
    {
        float nearDepth = SliceToViewDepth(float(z) / float(gGridSize.z));
        float farDepth = SliceToViewDepth(float(z + 1) / float(gGridSize.z));
        float stepLength = (farDepth - nearDepth) * rayScale;

        float4 scatter = gScatter[int3(column, z)];
        float extinction = max(scatter.a, 1e-6f);
        float stepTransmittance = exp(-extinction * stepLength);

        // スライスの中で減衰しながら溜まる分（減衰係数が大きくても明るくなりすぎない積分）
        float3 stepScattering = (scatter.rgb - scatter.rgb * stepTransmittance) / extinction;
        accumulated += transmittance * stepScattering;
        transmittance *= stepTransmittance;

        gIntegrated[int3(column, z)] = float4(accumulated, transmittance);
    }
}
