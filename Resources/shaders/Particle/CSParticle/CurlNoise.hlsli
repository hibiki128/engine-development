#ifndef CURL_NOISE_HLSLI
#define CURL_NOISE_HLSLI

// ============================================================
// Curl Noise Implementation for HLSL
// 
// Curl Noise はポテンシャル場のカールから発散ゼロの速度場を生成する。
// curl(F) = (∂Fz/∂y - ∂Fy/∂z, ∂Fx/∂z - ∂Fz/∂x, ∂Fy/∂x - ∂Fx/∂y)
//
// 各ポテンシャル成分に異なるオフセットのノイズを使用することで
// 独立した3成分を生成し、curl演算を数値微分で近似する。
// ============================================================

// ----------------------------------------------------------------
// Value Noise (軽量・GPU向け)
// hash関数ベースの格子ノイズ
// ----------------------------------------------------------------
float3 HashF3(float3 p)
{
    p = float3(
        dot(p, float3(127.1f, 311.7f, 74.7f)),
        dot(p, float3(269.5f, 183.3f, 246.1f)),
        dot(p, float3(113.5f, 271.9f, 124.6f))
    );
    return -1.0f + 2.0f * frac(sin(p) * 43758.5453123f);
}

// Gradient Noise (Perlin的)
float GradientNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    
    // Quintic smoothstep
    float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    
    return lerp(
        lerp(
            lerp(dot(HashF3(i + float3(0,0,0)), f - float3(0,0,0)),
                 dot(HashF3(i + float3(1,0,0)), f - float3(1,0,0)), u.x),
            lerp(dot(HashF3(i + float3(0,1,0)), f - float3(0,1,0)),
                 dot(HashF3(i + float3(1,1,0)), f - float3(1,1,0)), u.x),
            u.y),
        lerp(
            lerp(dot(HashF3(i + float3(0,0,1)), f - float3(0,0,1)),
                 dot(HashF3(i + float3(1,0,1)), f - float3(1,0,1)), u.x),
            lerp(dot(HashF3(i + float3(0,1,1)), f - float3(0,1,1)),
                 dot(HashF3(i + float3(1,1,1)), f - float3(1,1,1)), u.x),
            u.y),
        u.z);
}

// fBm (fractal Brownian motion) - オクターブ重ねでリッチなノイズに
float FBMNoise(float3 p, int octaves, float lacunarity, float gain)
{
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    
    [unroll(4)]
    for (int i = 0; i < octaves; ++i)
    {
        value += amplitude * GradientNoise(p * frequency);
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return value;
}

// ----------------------------------------------------------------
// Curl Noise 本体
// 数値微分でポテンシャル場のカールを近似する。
//
// 引数:
//   pos        : ワールド座標
//   scale      : ノイズのスケール（大きいほど渦が大きく）
//   timeOffset : 時間オフセット（gPerFrame.time * curlTimeScale を渡す）
//   octaves    : fBmのオクターブ数（1〜4推奨）
// 戻り値:
//   発散ゼロの速度ベクトル（正規化なし）
// ----------------------------------------------------------------
float3 ComputeCurlNoise(float3 pos, float scale, float timeOffset, int octaves)
{
    // 数値微分のオフセット量
    // 小さすぎると精度が落ち、大きすぎると誤差が増える
    const float eps = 0.01f;
    const float inv2eps = 1.0f / (2.0f * eps);
    
    // スケールを適用した座標＋時間オフセット
    float3 sp = pos * scale;
    float3 t = float3(timeOffset, timeOffset, timeOffset);
    
    // ポテンシャル場の3成分にオフセットを与えて独立させる
    // Px : z方向にオフセット 0.0
    // Py : z方向にオフセット 3.33
    // Pz : z方向にオフセット 6.66
    float3 offset0 = float3(0.0f,   0.0f,   0.0f);
    float3 offset1 = float3(3.33f,  1.78f,  2.91f);
    float3 offset2 = float3(6.66f,  3.56f,  5.82f);
    
    // --- ∂Pz/∂y, ∂Pz/∂x ---
    float Pz_yp = FBMNoise(sp + float3(0, eps, 0) + offset2 + t, octaves, 2.0f, 0.5f);
    float Pz_yn = FBMNoise(sp - float3(0, eps, 0) + offset2 + t, octaves, 2.0f, 0.5f);
    float dPzdy = (Pz_yp - Pz_yn) * inv2eps;
    
    float Pz_xp = FBMNoise(sp + float3(eps, 0, 0) + offset2 + t, octaves, 2.0f, 0.5f);
    float Pz_xn = FBMNoise(sp - float3(eps, 0, 0) + offset2 + t, octaves, 2.0f, 0.5f);
    float dPzdx = (Pz_xp - Pz_xn) * inv2eps;
    
    // --- ∂Py/∂z, ∂Py/∂x ---
    float Py_zp = FBMNoise(sp + float3(0, 0, eps) + offset1 + t, octaves, 2.0f, 0.5f);
    float Py_zn = FBMNoise(sp - float3(0, 0, eps) + offset1 + t, octaves, 2.0f, 0.5f);
    float dPydz = (Py_zp - Py_zn) * inv2eps;
    
    float Py_xp = FBMNoise(sp + float3(eps, 0, 0) + offset1 + t, octaves, 2.0f, 0.5f);
    float Py_xn = FBMNoise(sp - float3(eps, 0, 0) + offset1 + t, octaves, 2.0f, 0.5f);
    float dPydx = (Py_xp - Py_xn) * inv2eps;
    
    // --- ∂Px/∂y, ∂Px/∂z ---
    float Px_yp = FBMNoise(sp + float3(0, eps, 0) + offset0 + t, octaves, 2.0f, 0.5f);
    float Px_yn = FBMNoise(sp - float3(0, eps, 0) + offset0 + t, octaves, 2.0f, 0.5f);
    float dPxdy = (Px_yp - Px_yn) * inv2eps;
    
    float Px_zp = FBMNoise(sp + float3(0, 0, eps) + offset0 + t, octaves, 2.0f, 0.5f);
    float Px_zn = FBMNoise(sp - float3(0, 0, eps) + offset0 + t, octaves, 2.0f, 0.5f);
    float dPxdz = (Px_zp - Px_zn) * inv2eps;
    
    // curl = ( ∂Pz/∂y - ∂Py/∂z,
    //          ∂Px/∂z - ∂Pz/∂x,
    //          ∂Py/∂x - ∂Px/∂y )
    return float3(
        dPzdy - dPydz,
        dPxdz - dPzdx,
        dPydx - dPxdy
    );
}

#endif // CURL_NOISE_HLSLI
