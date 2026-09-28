#include "../Particle.hlsli"

// =============================================================
// GPUパーティクルのピクセルシェーダー
//
// shapeMode = 0 のときは従来どおりテクスチャをそのまま貼る。
// 1 以上のときは**テクスチャを一切読まず、形をこの場で計算して作る**。
//
// なぜそうするか:
//   格闘ゲームの炎・稲妻は「丸いソフトスプライトを大量に飛ばす」のでは作られておらず、
//   輪郭のある絵になっている。ただしその絵（スプライトシート）を描き起こすのは大変なので、
//   ここでは**形そのものを計算で作る**。画像リソースが1枚も要らない。
//   輪郭は smoothstep のしきい値で切るので、ぼかし量を小さくすればセル調のパキっとした縁になる。
//
// UVスクロールの扱い:
//   テクスチャ（mode 0）は読む位置そのものをずらす。
//   計算で形を作るモードは**ゆらぎの座標だけ**をずらす。
//   形の骨格（幅・先細り）まで流すと、絵が板からずり落ちてしまうため。
// =============================================================

struct Material
{
    float4 color;
    float4x4 uvTransform;
    // ---- プロシージャル形状。C++ ParticleMaterial と一致させること ----
    uint shapeMode;         // 0=テクスチャ / 1=炎 / 2=シャード(稲妻) / 3=リング / 4=メッシュ炎
    float shapeEdge;        // 輪郭のしきい値
    float shapeNoiseScale;  // ゆらぎの細かさ
    float shapeRimWidth;    // 縁取りの太さ
    float4 shapeRimColor;   // 縁の色（中心は白く抜ける）
    float shapeTime;        // 経過時間
    float shapeSpeed;       // ゆらぎが動く速さ
    float shapeSoftness;    // 輪郭のぼかし
    uint enableSoftParticle; // 1=背景に近いほど薄くする
    float softParticleFade;  // 完全に消えるまでの距離
    float depthProjM22;      // 深度→ビュー奥行きに戻すための射影行列要素
    float depthProjM32;
    float shapeFresnel;      // 縁ほど濃く・正面ほど薄くする強さ [0,1]
    float2 uvScrollSpeed;    // UVを流す速さ
    float shapePad2;
    float shapePad3;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

ConstantBuffer<Material> gMaterial : register(b1);
Texture2D<float4> gTexture : register(t0);
// 描画直前に複製した深度（ソフトパーティクル用）。使わないときも束ねておく
Texture2D<float> gSceneDepth : register(t1);
SamplerState gSampler : register(s0);

/// 板が背景へ刺さった「切り口の直線」を消すための減衰。
/// 背景との奥行きの差が softParticleFade より近いほど薄くする。
/// 1.0 を返せば何も変わらない
float SoftParticleFactor(float4 svPosition)
{
    if (gMaterial.enableSoftParticle == 0u)
    {
        return 1.0f;
    }
    // SV_POSITION の w は 1/クリップw。透視投影ではクリップw＝ビュー空間の奥行きなので、
    // その逆数がこの画素の奥行きになる（補間子を増やさずに取れる）
    float particleViewZ = 1.0f / max(svPosition.w, 1e-6f);

    float sceneDepth = gSceneDepth.Load(int3((int2) svPosition.xy, 0));
    float denom = sceneDepth - gMaterial.depthProjM22;
    // 遠クリップ相当（分母が 0 付近）は「背景が無限遠」とみなして減衰させない
    if (abs(denom) < 1e-6f)
    {
        return 1.0f;
    }
    float sceneViewZ = gMaterial.depthProjM32 / denom;

    float fade = max(gMaterial.softParticleFade, 1e-4f);
    return saturate((sceneViewZ - particleViewZ) / fade);
}

/// 縁の発光。輪郭（視線と面が平行）ほど 1、カメラ正面を向いた面ほど 1-shapeFresnel になる。
/// 体を包む殻に使うと、中のキャラが透けて輪郭だけが光る。0 なら 1.0 を返し何も変えない
float FresnelFactor(float facing)
{
    if (gMaterial.shapeFresnel <= 0.0f)
    {
        return 1.0f;
    }
    float edge = smoothstep(0.0f, 0.9f, 1.0f - saturate(facing));
    return lerp(1.0f, edge, saturate(gMaterial.shapeFresnel));
}

// ---- 計算で形を作るための小さなノイズ ----
float Hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}
float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f); // なめらかに繋ぐ
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
// 2オクターブで十分。増やすほど細かくなるが1画素あたりが重くなる
float Fbm2(float2 p)
{
    return ValueNoise(p) * 0.65f + ValueNoise(p * 2.13f + 7.3f) * 0.35f;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float fade = SoftParticleFactor(input.position) * FresnelFactor(input.facing);
    // 毎秒 uvScrollSpeed だけ進むずらし量
    float2 scroll = gMaterial.uvScrollSpeed * gMaterial.shapeTime;

    if (gMaterial.shapeMode == 0u)
    {
        // ---- 従来どおり: テクスチャをそのまま ----
        // CS パーティクルの uvTransform は常に単位行列のため、per-pixel の 4x4 変換を省略する。
        // （オーバードロー時はこの 1 画素あたりの行列積が積み上がるため、削るとフィルが軽くなる）
        float4 textureColor = gTexture.Sample(gSampler, input.texcoord + scroll);
        output.color = gMaterial.color * textureColor * input.color;
        output.color.a *= fade;
        if (output.color.a == 0.0f)
        {
            discard;
        }
        return output;
    }

    // ---- ここから先はテクスチャを読まない ----
    // 粒ごとに固定の種。同じ形が並ばないようにゆらぎをずらす
    float seed = input.seed * 0.017f;
    float t = gMaterial.shapeTime * gMaterial.shapeSpeed;
    float ns = max(gMaterial.shapeNoiseScale, 0.01f);
    float edgeSoft = max(gMaterial.shapeSoftness, 0.001f);

    // 形の骨格はスクロールさせない生の UV で決める（流すのはゆらぎだけ）
    float2 uv = input.texcoord;
    // v は上向きを 1 にそろえる（板ポリの UV は左上が原点なので反転する）
    float vUp = 1.0f - uv.y;
    float dx = abs(uv.x - 0.5f) * 2.0f; // 0=中心 / 1=左右の端

    float field = 0.0f;

    if (gMaterial.shapeMode == 1u)
    {
        // ---- 炎 ----
        // 根元が太く先が細い舌状。縁を縦に流れるノイズで揺らして「めらめら」させる。
        // 揺らぎは粗い波と細かい波の2枚。1枚だけだと輪郭がカクカクした多角形に見えてしまう
        float width = pow(saturate(1.0f - vUp), 0.55f);
        float n1 = Fbm2(float2(uv.x * ns + seed, vUp * ns * 1.6f - t) + scroll * ns);
        float n2 = Fbm2(float2(uv.x * ns * 2.7f + seed * 1.7f, vUp * ns * 3.1f - t * 1.7f) + scroll * ns * 2.0f);
        float wobble = ((n1 - 0.5f) * 0.7f + (n2 - 0.5f) * 0.35f) * (0.25f + vUp);
        field = saturate((width - dx + wobble) * 0.5f + 0.5f);
    }
    else if (gMaterial.shapeMode == 2u)
    {
        // ---- シャード（稲妻・尖った破片）----
        // 炎より鋭くとがらせ、横方向のブレを強めて「バチッ」とした見た目にする。
        float width = saturate(1.0f - vUp) * 0.9f;
        float n = Fbm2(float2(vUp * ns * 2.0f + seed, t) + scroll * ns);
        float jag = (n - 0.5f) * 0.55f;
        field = saturate((width - dx + jag) * 0.5f + 0.5f);
    }
    else if (gMaterial.shapeMode == 3u)
    {
        // ---- リング（衝撃波）----
        float2 c = uv - 0.5f;
        float r = length(c) * 2.0f;
        float ang = atan2(c.y, c.x);
        float n = Fbm2(float2(ang * ns, t + seed) + scroll * ns);
        float ringR = 0.82f + (n - 0.5f) * 0.22f;
        float thickness = max(gMaterial.shapeRimWidth, 0.02f);
        field = saturate(1.0f - abs(r - ringR) / thickness);
    }
    else
    {
        // ---- メッシュ炎（円錐・円柱へ貼って「一本の炎の筒」にする）----
        // 板ポリと違い、シルエットはメッシュそのものが持っている。
        // だからここでは幅を作らず、**流れるゆらぎでメッシュを食い破る**役に徹する。
        //   根元 … ゆらぎに下駄を履かせて必ず残す（芯が消えない）
        //   先端 … 下駄が無くなるのでゆらぎ任せ＝ちぎれて舌状になる
        //
        // 筒の UV は u が円周を 0→1 で一周するので、そのまま 2D ノイズへ入れると
        // 継ぎ目に一本線が出る。円周上の座標 (cos, sin) を使って回り込みを消す。
        const float kTau = 6.2831853f;
        float ang = uv.x * kTau;
        float2 ring = float2(cos(ang), sin(ang)) * ns;
        // 縦は生の v にスクロールを足す。uvScrollSpeed.y をマイナスにすると炎が立ち上る
        float flow = vUp * ns * 1.5f - t - scroll.y * ns;
        float n = Fbm2(float2(ring.x + seed, flow)) * 0.5f +
                  Fbm2(float2(ring.y + seed * 1.7f, flow + 31.7f)) * 0.5f;
        field = saturate(n * 0.65f + (1.0f - vUp) * 0.6f);
    }

    // 輪郭を切る。edgeSoft を小さくするほどセル調のパキっとした縁になる
    float alpha = smoothstep(gMaterial.shapeEdge - edgeSoft, gMaterial.shapeEdge + edgeSoft, field);
    if (alpha <= 0.0f)
    {
        discard;
    }

    // 縁取り: 内側は白く抜け、外周だけ shapeRimColor になる（DB系の2トーンの見え方）
    float rim = 1.0f - smoothstep(gMaterial.shapeEdge,
                                  gMaterial.shapeEdge + max(gMaterial.shapeRimWidth, 0.001f), field);
    float3 rgb = lerp(float3(1.0f, 1.0f, 1.0f), gMaterial.shapeRimColor.rgb, rim);

    output.color = float4(rgb, alpha * fade) * gMaterial.color * input.color;
    if (output.color.a == 0.0f)
    {
        discard;
    }
    return output;
}
