#include "FullScreen.hlsli"

// 打撃インパクト。格闘アニメの「当たった瞬間」の画面効果を1枚のパスでまとめて掛ける。
//   ・衝撃波リング … 当たった点から広がる輪で背景を押しのけて歪ませる（最大4つ同時）
//   ・集中ブラー   … 注目点へ向かって画面をブレさせる
//   ・色収差       … 注目点から離れるほど赤と青をずらす
//   ・白フラッシュ … 画面全体を白く飛ばす
//   ・インパクトフレーム … 明るさで2色に塗り分けた「白黒の1コマ」を差し込む
// どれも強さ0なら何もしないので、使う分だけ値を入れればよい。
// チェーンはトーンマップ前（リニアHDR）で動くので、色は1を超えてよい。

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

static const int kMaxWaves = 4;

struct ImpactParameters
{
    float4 waves[kMaxWaves];     // xy=中心UV / z=半径（画面の高さ=1） / w=歪みの強さ（UV）
    float4 waveShape[kMaxWaves]; // x=輪の太さ / y=輪の上の色収差 / z=輪の光 / w=未使用
    float2 focus;                // 集中ブラー・色収差の中心UV
    float radialBlur;            // 集中ブラーの強さ（注目点へ寄せる割合）
    float chroma;                // 色収差の強さ
    float flash;                 // 白フラッシュ量
    float impactFrame;           // インパクトフレームの混ぜ具合 0〜1
    float impactThreshold;       // 塗り分けの境目（トーンマップ後の明るさ 0〜1）
    float aspect;                // 画面の横/縦
    float4 impactDark;           // 暗い側に塗る色
    float4 impactLight;          // 明るい側に塗る色
    int waveCount;               // 使っている輪の数
    float3 pad;
};
ConstantBuffer<ImpactParameters> gImpact : register(b0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

/// 赤と青を逆向きにずらして読む。shift が0なら普通に1回読むのと同じ
float3 SampleSplit(float2 uv, float2 shift)
{
    float3 c;
    c.r = gTexture.Sample(gSampler, uv + shift).r;
    c.g = gTexture.Sample(gSampler, uv).g;
    c.b = gTexture.Sample(gSampler, uv - shift).b;
    return c;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    const float2 uv = input.texcoord;
    const float aspect = max(gImpact.aspect, 0.01f);
    const float2 toAspect = float2(aspect, 1.0f);

    // ---- 衝撃波リング ----
    // 輪の上の画素は、輪の内側（中心寄り）を読む。すると中の景色が外へ押し出されて見え、
    // 空気の壁が広がっていくように歪む。輪から外れると影響は0になる
    float2 offset = 0.0f;
    float2 waveShift = 0.0f;
    float glow = 0.0f;
    const int waveCount = min(gImpact.waveCount, kMaxWaves);
    for (int i = 0; i < waveCount; ++i)
    {
        const float4 wave = gImpact.waves[i];
        const float4 shape = gImpact.waveShape[i];

        // 円に見えるよう横方向を縦横比で補正してから距離を測る
        const float2 d = (uv - wave.xy) * toAspect;
        const float dist = length(d);
        if (dist < 1e-4f)
        {
            continue;
        }
        const float2 dir = d / dist;
        const float x = (dist - wave.z) / max(shape.x, 1e-3f);
        const float bump = exp(-x * x * 2.0f); // 輪の上で1、離れるとすぐ0

        // UV空間へ戻すときに横方向の補正を外す
        const float2 dirUv = dir / toAspect;
        offset += dirUv * wave.w * bump;
        waveShift += dirUv * shape.y * bump;
        glow += bump * shape.z;
    }
    const float2 baseUv = uv - offset;

    // ---- 色収差 ----
    // 注目点から離れるほど強く、さらに輪の上では輪の向きに沿ってずらす
    const float2 fromFocus = baseUv - gImpact.focus;
    const float2 shift = fromFocus * gImpact.chroma + waveShift;

    // ---- 集中ブラー ----
    float3 color;
    if (gImpact.radialBlur > 1e-4f)
    {
        static const int kTaps = 10;
        float3 sum = 0.0f;
        [unroll]
        for (int t = 0; t < kTaps; ++t)
        {
            const float k = (float)t / (float)(kTaps - 1);
            sum += SampleSplit(baseUv - fromFocus * gImpact.radialBlur * k, shift);
        }
        color = sum / (float)kTaps;
    }
    else
    {
        color = SampleSplit(baseUv, shift);
    }

    // ---- インパクトフレーム ----
    // トーンマップ後の見た目の明るさで2色に塗り分ける。
    // 明るい側と暗い側の色を入れ替えれば白黒反転のコマになる。
    // 塗り分けはフラッシュや輪の光を足す前の絵で行う（先に足すと画面全体が明るい側へ寄って真っ白になる）
    if (gImpact.impactFrame > 1e-4f)
    {
        const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        const float displayed = luminance / (1.0f + luminance);
        const float light = smoothstep(gImpact.impactThreshold - 0.03f, gImpact.impactThreshold + 0.03f, displayed);
        const float3 frameColor = lerp(gImpact.impactDark.rgb, gImpact.impactLight.rgb, light);
        output.color = float4(lerp(color, frameColor, saturate(gImpact.impactFrame)), 1.0f);
        return output;
    }

    // 輪そのものをうっすら光らせる（歪みだけだと暗い背景で見えないため）
    color += glow * float3(1.0f, 0.93f, 0.8f);

    // ---- 白フラッシュ ----
    // HDR なので加算で押し上げる。画面全体を均一に白くすると霧がかったように見えるので、
    // 当たった点（注目点）のまわりほど強く、離れた所にはほんの少しだけ乗せる
    const float2 fromFocusAspect = (uv - gImpact.focus) * toAspect;
    const float flashShape = lerp(0.15f, 1.0f, exp(-dot(fromFocusAspect, fromFocusAspect) * 8.0f));
    color += gImpact.flash * flashShape * 3.0f;

    output.color = float4(color, 1.0f);
    return output;
}
