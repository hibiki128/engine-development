// =============================================================
// ガウシアンブラー（分離2パス版・コンピュートシェーダー）
//
// 従来のピクセルシェーダー版は、ピクセルごとに gauss() を呼んで
// カーネル係数をその場で作り直していた（7x7 なら exp() が49回／ピクセル）。
// ここでは
//   ・係数は CPU 側で1回だけ計算して定数バッファで渡す
//   ・2次元の畳み込みを 横方向 → 縦方向 の2パスに分ける（分離可能性を利用）
// の2点で計算量を落としている。
// 7x7 の場合、タップ数は 49 → 7+7=14 になり、exp() は毎フレーム0回になる。
// =============================================================

// カーネル半径の上限。weights の配列サイズと対応する（半径7＝15タップまで）
static const int kMaxRadius = 7;

cbuffer GaussianParams : register(b0)
{
    int gRadius;       // カーネル半径（kernelSize = gRadius * 2 + 1）
    int gDirection;    // 0 = 横方向, 1 = 縦方向
    int2 gTextureSize; // 処理対象の解像度（ピクセル）
    // 正規化済みの重み。中心から外側へ weights[0]=中心, weights[i]=中心からi個ぶん離れた位置。
    // float4 単位で詰めてあるので、index/4 番目の [index%4] 成分を読む。
    float4 gWeights[(kMaxRadius + 1 + 3) / 4];
};

Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);

// gWeights を1次元の配列として読む
float GetWeight(int index)
{
    return gWeights[index / 4][index % 4];
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    // 解像度がスレッドグループの倍数とは限らないので、範囲外は捨てる
    if (pixel.x >= gTextureSize.x || pixel.y >= gTextureSize.y)
    {
        return;
    }

    // このパスで進む方向
    int2 step = (gDirection == 0) ? int2(1, 0) : int2(0, 1);

    // 中心
    float4 sourceColor = gSource.Load(int3(pixel, 0));
    float3 accumulated = sourceColor.rgb * GetWeight(0);

    // 左右（上下）対称にサンプルする。重みは共通なので1回の乗算で済む。
    [unroll(kMaxRadius)]
    for (int i = 1; i <= gRadius; ++i)
    {
        int2 offset = step * i;
        // 端は clamp（Load はサンプラーを通さないので自前でクランプする）
        int2 minPixel = clamp(pixel - offset, int2(0, 0), gTextureSize - int2(1, 1));
        int2 maxPixel = clamp(pixel + offset, int2(0, 0), gTextureSize - int2(1, 1));

        float3 neighborSum = gSource.Load(int3(minPixel, 0)).rgb + gSource.Load(int3(maxPixel, 0)).rgb;
        accumulated += neighborSum * GetWeight(i);
    }

    // 重みは CPU 側で正規化済みなので、ここで割る必要はない
    gOutput[pixel] = float4(accumulated, sourceColor.a);
}
