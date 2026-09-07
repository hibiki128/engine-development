#ifndef HAGINE_TOON_HLSLI
#define HAGINE_TOON_HLSLI

// ============================================================
// トゥーン（セル）シェーディング共通処理
//
//   前方描画（Object3d.PS）とディファード（DeferredLighting.PS）の両方から
//   include して、どちらの経路でも同じ絵になるようにしている。
//
//   考え方:
//     ・明るさ（N・L）を連続値のまま使わず、段（バンド）に量子化する
//     ・暗い側は黒に落とさず「影の色」で着色する。これがトゥーンらしさの本体
//     ・影（シャドウマップ）も同じ量子化に巻き込む。こうすると落ち影と
//       陰（terminator）が同じ色になり、貼り絵のような画になる
//     ・ハイライトとリムライトも境界を立てて面で出す
//
//   ※ C++ 側の ToonSettingsGPU（Engine/3d/light/ToonSettings.h）と
//     必ず同じ並びにすること
// ============================================================

struct ToonSettings
{
    float4 shadeColor;    // rgb=影側の色（アルベドに乗算） a=未使用
    float4 specularColor; // rgb=ハイライトの色 a=強さ
    float4 rimColor;      // rgb=リムライトの色 a=強さ

    float enabled;   // 0=OFF, 1=ON（グローバル切り替え）
    float steps;     // 段数。1で明暗の2階調、2で3階調…
    float threshold; // 明暗の境目の位置（0〜1、0.5が中央）
    float softness;  // 境目のなめらかさ。0に近いほど固いエッジ

    float specularThreshold; // ハイライトが出はじめる強さ
    float specularSoftness;  // ハイライトの境目のなめらかさ
    float rimPower;          // リムの絞り（大きいほど輪郭だけ）
    float rimThreshold;      // リムが出はじめる位置

    float rimSoftness;    // リムの境目のなめらかさ
    float rimLightMask;   // 1でリムを光の当たる側だけに出す
    float shadowSharpness; // 1で落ち影も量子化に巻き込む（0で従来どおりのなめらかな影）
    float padding;
};

/// <summary>
/// 連続的な明るさを段階に量子化する
/// </summary>
/// <param name="t">0〜1の明るさ</param>
/// <param name="s">トゥーン設定</param>
/// <returns>float: 量子化後の 0〜1 の明るさ</returns>
float ToonBand(float t, ToonSettings s)
{
    const float steps = max(s.steps, 1.0f);
    const float scaled = saturate(t) * steps;
    const float lower = floor(scaled);
    const float f = scaled - lower;

    // softness=0 だと smoothstep の上下端が一致して結果が定まらないので下限を入れる
    const float soft = max(s.softness, 1e-4f);
    const float w = smoothstep(s.threshold - soft, s.threshold + soft, f);

    return saturate((lower + w) / steps);
}

/// <summary>
/// ハイライトを面で出す（境界を立てたスペキュラ）
/// </summary>
/// <param name="normal">法線</param>
/// <param name="lightDir">ライトへ向かう単位ベクトル</param>
/// <param name="toEye">視線へ向かう単位ベクトル</param>
/// <param name="shininess">光沢度</param>
/// <param name="s">トゥーン設定</param>
/// <returns>float: 0〜1のハイライト量</returns>
float ToonSpecularAmount(float3 normal, float3 lightDir, float3 toEye, float shininess, ToonSettings s)
{
    const float3 halfVector = normalize(lightDir + toEye);
    const float raw = pow(saturate(dot(normal, halfVector)), max(shininess, 1.0f));
    const float soft = max(s.specularSoftness, 1e-4f);
    return smoothstep(s.specularThreshold - soft, s.specularThreshold + soft, raw);
}

/// <summary>
/// 主光源（平行光源）のトゥーン陰影。物体の基本色をここで決める。
/// 影側を黒に落とさず shadeColor で着色するのが要点。
/// </summary>
/// <param name="albedo">アルベド</param>
/// <param name="normal">法線</param>
/// <param name="lightDir">ライトへ向かう単位ベクトル</param>
/// <param name="toEye">視線へ向かう単位ベクトル</param>
/// <param name="lightColor">ライト色</param>
/// <param name="intensity">輝度</param>
/// <param name="shadowFactor">シャドウマップの遮蔽（1=当たっている）</param>
/// <param name="shininess">光沢度</param>
/// <param name="s">トゥーン設定</param>
/// <param name="outBand">量子化後の明るさ（リムのマスクに使う）</param>
/// <returns>float3: 加算する色</returns>
float3 ToonMainLight(float3 albedo, float3 normal, float3 lightDir, float3 toEye,
                     float3 lightColor, float intensity, float shadowFactor,
                     float shininess, ToonSettings s, out float outBand)
{
    // ハーフランバートを 0〜1 の素の明るさとして使う（裏面がいきなり真っ黒にならない）
    const float halfLambert = dot(normal, lightDir) * 0.5f + 0.5f;

    // 落ち影を量子化の前に掛けておくと、影の縁も同じ段で切られて硬いトゥーン影になる。
    // shadowSharpness=0 なら従来どおり量子化のあとに掛けてなめらかな影のままにする。
    const float shadowIn = lerp(1.0f, shadowFactor, s.shadowSharpness);
    const float shadowOut = lerp(shadowFactor, 1.0f, s.shadowSharpness);

    const float band = ToonBand(halfLambert * shadowIn, s);
    outBand = band;

    // 影側は黒ではなく shadeColor を掛けた色にする
    const float3 diffuse = albedo * lerp(s.shadeColor.rgb, float3(1.0f, 1.0f, 1.0f), band);

    // ハイライトは光の当たっている段でだけ出す（影の中で光らせない）
    const float specular = ToonSpecularAmount(normal, lightDir, toEye, shininess, s) * band;

    return (diffuse + s.specularColor.rgb * s.specularColor.a * specular) *
           lightColor * intensity * shadowOut;
}

/// <summary>
/// 補助光源（点光源・スポットライト）のトゥーン陰影。
/// 主光源が基本色を作っているので、こちらは明るい側にだけ足す
/// （影の色まで光源ごとに足すと、光源が増えるほど影が明るくなってしまう）。
/// </summary>
/// <param name="albedo">アルベド</param>
/// <param name="normal">法線</param>
/// <param name="lightDir">ライトへ向かう単位ベクトル</param>
/// <param name="toEye">視線へ向かう単位ベクトル</param>
/// <param name="lightColor">ライト色</param>
/// <param name="intensity">輝度</param>
/// <param name="attenuation">距離・角度による減衰</param>
/// <param name="shininess">光沢度</param>
/// <param name="s">トゥーン設定</param>
/// <returns>float3: 加算する色</returns>
float3 ToonAddLight(float3 albedo, float3 normal, float3 lightDir, float3 toEye,
                    float3 lightColor, float intensity, float attenuation,
                    float shininess, ToonSettings s)
{
    const float band = ToonBand(saturate(dot(normal, lightDir)), s);
    const float specular = ToonSpecularAmount(normal, lightDir, toEye, shininess, s) * band;

    return (albedo * band + s.specularColor.rgb * s.specularColor.a * specular) *
           lightColor * intensity * attenuation;
}

/// <summary>
/// リムライト（輪郭の光）。トゥーンでは境界を立てて面で出す。
/// </summary>
/// <param name="normal">法線</param>
/// <param name="toEye">視線へ向かう単位ベクトル</param>
/// <param name="lightBand">主光源の量子化後の明るさ（光の当たる側だけに出すためのマスク）</param>
/// <param name="s">トゥーン設定</param>
/// <returns>float3: 加算する色</returns>
float3 ToonRimLight(float3 normal, float3 toEye, float lightBand, ToonSettings s)
{
    const float facing = 1.0f - saturate(dot(normal, toEye));
    const float raw = pow(facing, max(s.rimPower, 0.01f));
    const float soft = max(s.rimSoftness, 1e-4f);
    float rim = smoothstep(s.rimThreshold - soft, s.rimThreshold + soft, raw);

    // rimLightMask=1 で「光が当たっている側の輪郭」だけが光る
    rim *= lerp(1.0f, lightBand, saturate(s.rimLightMask));

    return s.rimColor.rgb * s.rimColor.a * rim;
}

#endif // HAGINE_TOON_HLSLI
