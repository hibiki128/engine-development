// =============================================================
// 深度ベースのアウトライン（コンピュートシェーダー版）
//
// 旧版（DepthBasedOutline.PS.hlsl）がうまく出なかった原因:
//   Prewitt フィルタを「生のビュー空間Z」に掛けていたため、差分の大きさが
//   カメラからの距離にそのまま比例していた。
//   → 近くの物ほど輪郭が出ず、遠景は一面が輪郭になる。
//     それを saturate(weight / 6.0f) という固定値で無理やり抑えていた。
//
// ここでは
//   ・深度差を「中心画素の深度」で割って相対値にする（距離に依存しない）
//   ・面の傾きによる深度差を輪郭と誤検出しないよう、反対側の画素から
//     予測した深度と比べる（depth discontinuity 判定）
//   ・輪郭の色・太さ・しきい値をパラメータで出す
// としている。
// =============================================================

cbuffer OutlineParams : register(b0)
{
    float4x4 gProjectionInverse; // 射影行列の逆行列（NDC深度 → ビュー空間Z）
    float4 gOutlineColor;        // 輪郭の色（rgb）と濃さ（a）
    float gThreshold;            // 輪郭とみなす相対深度差。小さいほど輪郭が増える
    float gThickness;            // 輪郭の太さ（ピクセル）
    int2 gTextureSize;           // 処理対象の解像度
};

Texture2D<float4> gSource : register(t0);
Texture2D<float> gDepth : register(t1);
RWTexture2D<float4> gOutput : register(u0);

/// NDCの深度からビュー空間の直線的な深度（カメラからの距離）を求める
float LinearizeDepth(float ndcDepth)
{
    // 標準的な射影行列ではビューZはNDCのxyに依存しないので、xy=0で計算してよい
    float4 viewSpace = mul(float4(0.0f, 0.0f, ndcDepth, 1.0f), gProjectionInverse);
    return viewSpace.z / max(viewSpace.w, 1e-6f);
}

/// 範囲内にクランプして深度を読む
float SampleLinearDepth(int2 pixel)
{
    int2 clamped = clamp(pixel, int2(0, 0), gTextureSize - int2(1, 1));
    return LinearizeDepth(gDepth.Load(int3(clamped, 0)));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gTextureSize.x || pixel.y >= gTextureSize.y)
    {
        return;
    }

    float4 sourceColor = gSource.Load(int3(pixel, 0));

    float rawDepth = gDepth.Load(int3(pixel, 0));
    // 深度が書かれていない場所（空など）は輪郭を描かない
    if (rawDepth >= 1.0f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }

    float centerDepth = LinearizeDepth(rawDepth);
    int offset = max(1, (int)gThickness);

    // 上下・左右の対で見る。
    // 平らな斜面では「両隣の平均 ≒ 中心」になるので輪郭にならず、
    // 物の縁のように深度が途切れている場所だけ大きな差になる。
    float horizontalNear = SampleLinearDepth(pixel + int2(-offset, 0));
    float horizontalFar = SampleLinearDepth(pixel + int2(offset, 0));
    float verticalNear = SampleLinearDepth(pixel + int2(0, -offset));
    float verticalFar = SampleLinearDepth(pixel + int2(0, offset));

    float horizontalDiff = abs((horizontalNear + horizontalFar) * 0.5f - centerDepth);
    float verticalDiff = abs((verticalNear + verticalFar) * 0.5f - centerDepth);
    float depthDifference = max(horizontalDiff, verticalDiff);

    // 中心の深度で割って相対値にする。これで遠近によらず同じしきい値が使える。
    float relativeDifference = depthDifference / max(abs(centerDepth), 1e-4f);

    // しきい値の前後でなめらかに立ち上げる（ジャギーを抑える）
    float edge = smoothstep(gThreshold, gThreshold * 2.0f, relativeDifference);

    float3 outlined = lerp(sourceColor.rgb, gOutlineColor.rgb, edge * gOutlineColor.a);
    gOutput[pixel] = float4(outlined, sourceColor.a);
}
