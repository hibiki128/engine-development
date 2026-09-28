// =============================================================
// SSR（画面内反射 / Screen Space Reflection）
//
// 画面に写っているものだけを使って、床や水面に周囲を映り込ませる。
// 各ピクセルの法線で視線を反射させ、その向きへ少しずつ進みながら
// 「その位置は深度バッファ上の物より奥に潜ったか」を調べる。
// 潜った瞬間が交点なので、そこの色を持ってきて元の絵へ混ぜる。
//
// 画面の外にあるものは映せない（情報が無いため）。
// レイが画面外へ出たり、何にも当たらなかった場合は反射を弱めて誤魔化す。
//
// 必要なもの: シーンの深度と、G-Buffer の法線。
// → **ディファードが有効なときだけ動く。** 無効なら素通しする
// =============================================================

cbuffer SsrParams : register(b0)
{
    float4x4 gViewProjection;        // ワールド → クリップ
    float4x4 gInverseViewProjection; // NDC → ワールド
    float3 gCameraPosition;          // カメラのワールド座標
    float gIntensity;                // 反射の強さ
    float gMaxDistance;              // レイを飛ばす最大距離（ワールド単位）
    float gThickness;                // 交点とみなす深度差の許容量
    float gEdgeFade;                 // 画面端で反射を弱める幅（0〜0.5）
    float gFresnelPower;             // 斜めから見たときに強くなる度合い
    float gReflectance;              // 正面から見たときの反射率（F0）
    float gStepJitter;               // レイの開始位置をずらす量（縞対策）
    int gStepCount;                  // レイマーチの分割数
    int gHasNormals;                 // G-Bufferの法線が使えるか（0なら素通し）
    int2 gTextureSize;               // 処理対象の解像度
    float2 gPadding;
};

Texture2D<float4> gSource : register(t0);
Texture2D<float> gDepth : register(t1);
Texture2D<float4> gNormal : register(t2);
RWTexture2D<float4> gOutput : register(u0);

SamplerState gSamplerPoint : register(s1);

/// 深度バッファの値からワールド座標を復元する
float3 ReconstructWorldPosition(float2 uv, float rawDepth)
{
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, rawDepth, 1.0f);
    float4 world = mul(ndc, gInverseViewProjection);
    return world.xyz / (abs(world.w) < 1e-6f ? 1e-6f : world.w);
}

/// ワールド座標を画面UVと深度へ移す
/// 戻り値 xy=UV, z=NDC深度, w=クリップw（カメラの後ろなら0以下）
float4 ProjectToScreen(float3 worldPosition)
{
    float4 clip = mul(float4(worldPosition, 1.0f), gViewProjection);
    if (clip.w <= 1e-6f)
    {
        return float4(0.0f, 0.0f, 0.0f, -1.0f);
    }
    float3 ndc = clip.xyz / clip.w;
    return float4(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f, ndc.z, clip.w);
}

/// 画素ごとに散らばる 0〜1 の値（光の筋と同じ手口）
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

/// 画面の端に近いほど 0 に近づく係数。映り込みが画面外で唐突に切れるのを隠す
float ScreenEdgeFade(float2 uv)
{
    float fade = saturate(gEdgeFade);
    if (fade <= 0.0f)
    {
        return 1.0f;
    }
    float2 distanceToEdge = min(uv, 1.0f - uv);
    float2 factor = saturate(distanceToEdge / fade);
    return factor.x * factor.y;
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

    // 法線が無い（ディファード無効）ときと、強さ0のときは何もしない
    float rawDepth = gDepth.Load(int3(pixel, 0));
    if (gHasNormals == 0 || gIntensity <= 0.0f || rawDepth >= 1.0f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }

    float2 uv = (float2(pixel) + 0.5f) / float2(gTextureSize);
    float3 worldPosition = ReconstructWorldPosition(uv, rawDepth);

    // 前方描画で描かれた物（ライティングOFFなど）は G-Buffer に法線が書かれていない。
    // そのまま normalize すると 0除算で NaN になり、画面が壊れる
    float4 normalSample = gNormal.Load(int3(pixel, 0));
    if (dot(normalSample.xyz, normalSample.xyz) < 1e-6f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }
    float3 normal = normalize(normalSample.xyz);

    float3 viewDirection = normalize(worldPosition - gCameraPosition);
    float3 reflectDirection = reflect(viewDirection, normal);

    // カメラ側へ跳ね返らない向き（自分の裏側へ潜る）は映しようがない
    if (dot(reflectDirection, normal) <= 0.0f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }

    int stepCount = clamp(gStepCount, 1, 128);
    float stepSize = gMaxDistance / stepCount;

    // 開始位置を画素ごとにずらして、分割の縞が出ないようにする
    float jitter = InterleavedGradientNoise(float2(pixel)) * saturate(gStepJitter);

    float3 hitColor = float3(0.0f, 0.0f, 0.0f);
    float hitFade = 0.0f;

    for (int i = 1; i <= stepCount; ++i)
    {
        float3 samplePosition = worldPosition + reflectDirection * ((i + jitter) * stepSize);
        float4 projected = ProjectToScreen(samplePosition);

        // カメラの後ろへ回ったら打ち切り
        if (projected.w <= 0.0f)
        {
            break;
        }
        // 画面の外に出たら打ち切り（画面外の情報は持っていない）
        if (projected.x < 0.0f || projected.x > 1.0f || projected.y < 0.0f || projected.y > 1.0f)
        {
            break;
        }

        float sceneRawDepth = gDepth.SampleLevel(gSamplerPoint, projected.xy, 0);
        if (sceneRawDepth >= 1.0f)
        {
            continue; // 空。まだ何にも当たっていない
        }

        // レイが「画面に写っている面より奥」に入ったらそこが交点。
        // ただし奥に入りすぎている場合は、手前の物の裏側をすり抜けただけなので採らない
        if (projected.z > sceneRawDepth)
        {
            float3 scenePosition = ReconstructWorldPosition(projected.xy, sceneRawDepth);
            float penetration = distance(samplePosition, scenePosition);
            if (penetration <= gThickness)
            {
                hitColor = gSource.SampleLevel(gSamplerPoint, projected.xy, 0).rgb;
                // 画面端と、遠くまで飛んだレイほど弱める
                hitFade = ScreenEdgeFade(projected.xy) *
                          saturate(1.0f - (float(i) / stepCount));
            }
            break;
        }
    }

    if (hitFade <= 0.0f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }

    // 斜めから見るほど強く映り込む（フレネル）
    float cosTheta = saturate(dot(-viewDirection, normal));
    float fresnel = gReflectance + (1.0f - gReflectance) * pow(1.0f - cosTheta, max(gFresnelPower, 0.1f));

    float3 reflection = hitColor * (fresnel * hitFade * gIntensity);

    gOutput[pixel] = float4(sourceColor.rgb + reflection, sourceColor.a);
}
