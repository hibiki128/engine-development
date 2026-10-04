// =============================================================
// シーン遷移
//
// 画面（UI まで合成した最終結果）に、遷移の幕を重ねる。
//   1. 下の画面を崩す（モザイク・ぼかし・渦・ズーム・回転・色ずれ・揺れ…）
//   2. 幕のレイヤーを下から順に重ねる（最大4枚）
//
// 幕の形は「その画素がどの順番で覆われるか」を 0〜1 の値（v）で表す。
// 進み具合 progress が v を超えた画素から覆われていく。
// 例えば円のアイリスなら v = 中心からの距離、ワイプなら v = 横の位置。
// 形を足すときは ShapeValue に分岐を1つ足すだけでよい。
//
// 入力:
//   t0      遷移を掛ける前の画面
//   t1      切り替える直前の画面（前の画面で塗る幕に使う）
//   t2〜t5  レイヤーごとのルール画像（明るさの順に覆う）
//   t6〜t9  レイヤーごとの塗りの画像
// =============================================================

#define MAX_LAYERS 4

struct Layer
{
    float4 color;        // 塗りの色（グラデーションの始まり・画像の色）
    float4 color2;       // グラデーションの終わり
    float4 edgeColor;    // ふちの光の色（a は明るさ）
    float4 palette[4];   // マスごとに塗り分ける色
    float2 center;       // 中心（UV）
    float angle;         // 向き（ラジアン）
    float count;         // 数（帯・マス・とげ・巻き数など）
    float progress;      // 進み具合（0=覆っていない / 1=覆いきった）
    float softness;      // 境目のぼかし
    float edgeWidth;     // ふちの光の幅
    float opacity;       // 濃さ
    int shape;           // 形
    int fill;            // 塗り方
    int invert;          // 1なら順番を逆にする
    int order;           // マスの埋まる順番
    float cellSpread;    // マスの埋まり始めのばらつき
    float amplitude;     // 波の大きさ
    float seed;          // 乱数の種
    float gradientAngle; // グラデーションの向き（ラジアン）
    float imageScale;    // 塗りの画像の繰り返し
    int paletteCount;    // 使う色の数
    int hasRule;         // ルール画像があるか
    int hasImage;        // 塗りの画像があるか
};

cbuffer TransitionParams : register(b0)
{
    int2 gTextureSize;
    float gTime;
    float gDeltaTime;
    int gLayerCount;
    int gHasSnapshot;
    float2 gPadding0;
    // 下の画面の崩し方（量は CPU 側で進み具合を掛けてある）
    float gMosaic;     // モザイクの大きさ（ピクセル）
    float gBlur;       // ぼかし（ピクセル）
    float gSwirl;      // 渦（ラジアン）
    float gZoom;       // ズーム（+で寄る）
    float gZoomBlur;   // 中心へ流れるぼかし
    float gRotate;     // 回転（ラジアン）
    float gChroma;     // 色ずれ（ピクセル）
    float gDesaturate; // 色を抜く
    float gBrightness; // +で白へ / -で黒へ
    float gShake;      // 揺れ（ピクセル）
    float gWave;       // 波打ち（ピクセル）
    float gPadding1;
    Layer gLayers[MAX_LAYERS];
};

Texture2D<float4> gScene : register(t0);
Texture2D<float4> gSnapshot : register(t1);
Texture2D<float4> gRule0 : register(t2);
Texture2D<float4> gRule1 : register(t3);
Texture2D<float4> gRule2 : register(t4);
Texture2D<float4> gRule3 : register(t5);
Texture2D<float4> gImage0 : register(t6);
Texture2D<float4> gImage1 : register(t7);
Texture2D<float4> gImage2 : register(t8);
Texture2D<float4> gImage3 : register(t9);
RWTexture2D<float4> gOutput : register(u0);
SamplerState gClampSampler : register(s0);
SamplerState gWrapSampler : register(s1);

static const float kPi = 3.14159265f;
static const float kTwoPi = 6.28318531f;

// 形の番号（C++ の TransitionShape と同じ並び）
#define SHAPE_FADE 0
#define SHAPE_WIPE 1
#define SHAPE_IRIS 2
#define SHAPE_BOX 3
#define SHAPE_DIAMOND 4
#define SHAPE_CLOCK 5
#define SHAPE_BLINDS 6
#define SHAPE_BARS 7
#define SHAPE_SPLIT 8
#define SHAPE_TILES 9
#define SHAPE_HEXAGONS 10
#define SHAPE_TRIANGLES 11
#define SHAPE_DOTS 12
#define SHAPE_DISSOLVE 13
#define SHAPE_SPIRAL 14
#define SHAPE_STAR 15
#define SHAPE_HEART 16
#define SHAPE_WAVE 17
#define SHAPE_FAN 18
#define SHAPE_RULE 19

// 塗り方の番号（C++ の TransitionFill と同じ並び）
#define FILL_COLOR 0
#define FILL_GRADIENT 1
#define FILL_PALETTE 2
#define FILL_IMAGE 3
#define FILL_PREVIOUS 4

// マスの埋まる順番（C++ の TransitionOrder と同じ並び）
#define ORDER_RANDOM 0
#define ORDER_SWEEP 1
#define ORDER_CENTER 2
#define ORDER_ALTERNATE 3

// ---------- 道具 ----------

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash12(i);
    float b = Hash12(i + float2(1.0f, 0.0f));
    float c = Hash12(i + float2(0.0f, 1.0f));
    float d = Hash12(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float Fbm(float2 p)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    for (int i = 0; i < 4; ++i)
    {
        sum += ValueNoise(p) * amplitude;
        p = p * 2.03f + float2(17.1f, 9.7f);
        amplitude *= 0.5f;
    }
    return sum / 0.9375f;
}

float Aspect()
{
    return float(gTextureSize.x) / max(float(gTextureSize.y), 1.0f);
}

/// UV を「縦が1・横が aspect」の空間へ移し、center を原点にする
float2 ToAspect(float2 uv, float2 center)
{
    return (uv - center) * float2(Aspect(), 1.0f);
}

float2 Rotate2D(float2 v, float a)
{
    float s = sin(a);
    float c = cos(a);
    return float2(v.x * c - v.y * s, v.x * s + v.y * c);
}

/// 中心から画面の四隅までのいちばん遠い距離（円・四角などを 0〜1 に収めるため）
float MaxCornerDistance(float2 center, int metric, float angle)
{
    float result = 0.0f;
    const float2 corners[4] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1)};
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 p = Rotate2D(ToAspect(corners[i], center), -angle);
        float d = (metric == 0) ? length(p) : (metric == 1) ? max(abs(p.x), abs(p.y)) : (abs(p.x) + abs(p.y));
        result = max(result, d);
    }
    return max(result, 1e-4f);
}

/// 向き dir に沿った画面上の位置（0〜1）
float Along(float2 uv, float2 dir)
{
    float2 q = ToAspect(uv, float2(0.5f, 0.5f));
    float extent = 0.5f * (Aspect() * abs(dir.x) + abs(dir.y));
    return saturate(dot(q, dir) / max(extent, 1e-4f) * 0.5f + 0.5f);
}

/// マスの埋まる順番（0〜1）
float CellOrder(Layer layer, float2 cellCenterUv, float2 cellId)
{
    if (layer.order == ORDER_SWEEP)
    {
        return Along(cellCenterUv, float2(cos(layer.angle), sin(layer.angle)));
    }
    if (layer.order == ORDER_CENTER)
    {
        return saturate(length(ToAspect(cellCenterUv, layer.center)) / MaxCornerDistance(layer.center, 0, 0.0f));
    }
    if (layer.order == ORDER_ALTERNATE)
    {
        // 市松に2回に分けて埋まる
        return (fmod(abs(cellId.x + cellId.y), 2.0f) < 0.5f) ? 0.0f : 1.0f;
    }
    return Hash12(cellId + layer.seed * 17.31f);
}

/// マスの形の距離（中心0 → ふち1）と、埋まる順番を合わせて v にする
float CellValue(Layer layer, float order, float local)
{
    float spread = saturate(layer.cellSpread);
    return order * spread + saturate(local) * (1.0f - spread);
}

float SdHeart(float2 p)
{
    p.x = abs(p.x);
    if (p.y + p.x > 1.0f)
    {
        return length(p - float2(0.25f, 0.75f)) - 0.35355339f;
    }
    float2 a = p - float2(0.0f, 1.0f);
    float2 b = p - 0.5f * max(p.x + p.y, 0.0f);
    return sqrt(min(dot(a, a), dot(b, b))) * sign(p.x - p.y);
}

float RuleValue(int index, float2 uv)
{
    float3 c;
    if (index == 0)
        c = gRule0.SampleLevel(gClampSampler, uv, 0.0f).rgb;
    else if (index == 1)
        c = gRule1.SampleLevel(gClampSampler, uv, 0.0f).rgb;
    else if (index == 2)
        c = gRule2.SampleLevel(gClampSampler, uv, 0.0f).rgb;
    else
        c = gRule3.SampleLevel(gClampSampler, uv, 0.0f).rgb;
    // 画像は sRGB として読まれて直線の明るさになっているので、描いたときの明るさへ戻す
    return pow(saturate(dot(c, float3(0.2126f, 0.7152f, 0.0722f))), 1.0f / 2.2f);
}

float4 ImageValue(int index, float2 uv)
{
    if (index == 0)
        return gImage0.SampleLevel(gWrapSampler, uv, 0.0f);
    if (index == 1)
        return gImage1.SampleLevel(gWrapSampler, uv, 0.0f);
    if (index == 2)
        return gImage2.SampleLevel(gWrapSampler, uv, 0.0f);
    return gImage3.SampleLevel(gWrapSampler, uv, 0.0f);
}

/// その画素が覆われる順番 v（0〜1）。cellId はマスで塗り分けるときに使う
float ShapeValue(Layer layer, int index, float2 uv, out float2 cellId)
{
    float aspect = Aspect();
    float2 dir = float2(cos(layer.angle), sin(layer.angle));
    float2 p = Rotate2D(ToAspect(uv, layer.center), -layer.angle);
    float count = max(layer.count, 1.0f);
    cellId = floor(uv * float2(aspect, 1.0f) * 6.0f);

    switch (layer.shape)
    {
    case SHAPE_WIPE:
        return Along(uv, dir);
    case SHAPE_IRIS:
        return length(p) / MaxCornerDistance(layer.center, 0, layer.angle);
    case SHAPE_BOX:
        return max(abs(p.x), abs(p.y)) / MaxCornerDistance(layer.center, 1, layer.angle);
    case SHAPE_DIAMOND:
        return (abs(p.x) + abs(p.y)) / MaxCornerDistance(layer.center, 2, layer.angle);
    case SHAPE_CLOCK:
    {
        float a = atan2(p.y, p.x) / kTwoPi + 0.25f; // 12時の位置から
        return frac(frac(a) * count);
    }
    case SHAPE_BLINDS:
    {
        float t = Along(uv, dir) * count;
        cellId = float2(floor(t), 0.0f);
        return frac(t);
    }
    case SHAPE_BARS:
    {
        // 向きと直角に並んだ帯が、順番をずらして滑り込む
        float2 perpendicular = float2(-dir.y, dir.x);
        float band = floor(Along(uv, perpendicular) * count);
        float along = Along(uv, dir);
        if (layer.order == ORDER_ALTERNATE && fmod(band, 2.0f) > 0.5f)
        {
            along = 1.0f - along; // 1本おきに反対側から
        }
        cellId = float2(band, 0.0f);
        float order = (layer.order == ORDER_ALTERNATE || layer.order == ORDER_SWEEP) ? band / max(count - 1.0f, 1.0f)
                                                                                    : Hash12(float2(band, layer.seed));
        if (layer.order == ORDER_CENTER)
        {
            order = abs(band / max(count - 1.0f, 1.0f) - 0.5f) * 2.0f;
        }
        return CellValue(layer, order, along);
    }
    case SHAPE_SPLIT:
    {
        float t = Along(uv, dir) * 2.0f - 1.0f;
        return 1.0f - abs(t);
    }
    case SHAPE_TILES:
    case SHAPE_DOTS:
    {
        float2 q = uv * float2(aspect, 1.0f) * count;
        cellId = floor(q);
        float2 local = frac(q) - 0.5f;
        float2 cellCenterUv = (cellId + 0.5f) / count / float2(aspect, 1.0f);
        float distance = (layer.shape == SHAPE_TILES) ? max(abs(local.x), abs(local.y)) * 2.0f : length(local) * 1.41421356f;
        return CellValue(layer, CellOrder(layer, cellCenterUv, cellId), distance);
    }
    case SHAPE_HEXAGONS:
    {
        // 隣の中心までの距離が1マスの六角形の格子
        const float2 s = float2(1.0f, 1.7320508f);
        float2 q = uv * float2(aspect, 1.0f) * count;
        float4 hc = floor(float4(q, q - float2(0.5f, 1.0f)) / s.xyxy) + 0.5f;
        float2 a = hc.xy * s;
        float2 b = (hc.zw + 0.5f) * s;
        bool useA = dot(q - a, q - a) < dot(q - b, q - b);
        float2 center = useA ? a : b;
        cellId = useA ? hc.xy : hc.zw + 1000.0f;
        float2 local = abs(q - center);
        float hexDistance = max(dot(local, float2(0.5f, 0.8660254f)), local.x) * 2.0f;
        float2 cellCenterUv = center / count / float2(aspect, 1.0f);
        return CellValue(layer, CellOrder(layer, cellCenterUv, cellId), hexDistance);
    }
    case SHAPE_TRIANGLES:
    {
        float2 q = uv * float2(aspect, 1.0f) * count;
        float2 index = floor(q);
        float2 local = frac(q);
        bool upper = (local.x + local.y) < 1.0f;
        cellId = index * 2.0f + (upper ? 0.0f : 1.0f);
        float2 triangleCenter = upper ? float2(1.0f / 3.0f, 1.0f / 3.0f) : float2(2.0f / 3.0f, 2.0f / 3.0f);
        float edge = upper ? min(min(local.x, local.y), 1.0f - local.x - local.y)
                           : min(min(1.0f - local.x, 1.0f - local.y), local.x + local.y - 1.0f);
        float2 cellCenterUv = (index + triangleCenter) / count / float2(aspect, 1.0f);
        // 重心からふちまでの距離は 1/3。重心で0、ふちで1
        return CellValue(layer, CellOrder(layer, cellCenterUv, cellId), 1.0f - edge * 3.0f);
    }
    case SHAPE_DISSOLVE:
    {
        float n = Fbm(uv * float2(aspect, 1.0f) * count + layer.seed * 13.7f);
        return saturate((n - 0.2f) / 0.6f);
    }
    case SHAPE_SPIRAL:
    {
        float r = length(p) / MaxCornerDistance(layer.center, 0, layer.angle);
        float a = frac(atan2(p.y, p.x) / kTwoPi + 1.0f);
        return saturate((r * count + a) / (count + 1.0f));
    }
    case SHAPE_STAR:
    {
        // とげの先で1、谷で0.5になる係数で距離を割ると、等高線が星形になる
        float theta = atan2(p.x, -p.y);
        float spike = 0.5f + 0.5f * pow(abs(cos(theta * count * 0.5f)), 3.0f);
        return saturate(length(p) / spike / (MaxCornerDistance(layer.center, 0, layer.angle) * 2.0f));
    }
    case SHAPE_HEART:
    {
        // ハートの中心から見た「その向きのふちまでの距離」で割ると、どの大きさでもハートの形の等高線になる。
        // ふちまでの距離は、距離場の符号が変わる所を二分探索で求める
        float2 h = float2(p.x, -p.y); // 上向きの座標へ
        float len = length(h);
        float2 dir = (len > 1e-5f) ? h / len : float2(0.0f, 1.0f);
        const float2 heartCenter = float2(0.0f, 0.55f);
        float lo = 0.0f;
        float hi = 1.5f;
        [unroll]
        for (int k = 0; k < 10; ++k)
        {
            float mid = (lo + hi) * 0.5f;
            if (SdHeart(heartCenter + dir * mid) < 0.0f)
                lo = mid;
            else
                hi = mid;
        }
        // 中心からふちまでの最短はおよそ 0.42（上のくぼみ）。四隅まで覆いきるよう、その比で割る
        float radius = max(lo, 0.05f);
        return saturate(len / radius / (MaxCornerDistance(layer.center, 0, layer.angle) / 0.42f));
    }
    case SHAPE_WAVE:
    {
        float2 perpendicular = float2(-dir.y, dir.x);
        float wave = sin(Along(uv, perpendicular) * count * kTwoPi + gTime * 4.0f) * layer.amplitude;
        float amplitude = abs(layer.amplitude);
        return saturate((Along(uv, dir) + wave + amplitude) / (1.0f + amplitude * 2.0f));
    }
    case SHAPE_FAN:
    {
        float a = frac(atan2(p.y, p.x) / kTwoPi + 1.0f) * count;
        cellId = float2(floor(a), 0.0f);
        return frac(a);
    }
    case SHAPE_RULE:
        return (layer.hasRule != 0) ? RuleValue(index, uv) : Along(uv, dir);
    default:
        return 0.5f;
    }
}

float3 FillColor(Layer layer, int index, float2 uv, float2 cellId)
{
    if (layer.fill == FILL_GRADIENT)
    {
        float t = Along(uv, float2(cos(layer.gradientAngle), sin(layer.gradientAngle)));
        return lerp(layer.color.rgb, layer.color2.rgb, t);
    }
    if (layer.fill == FILL_PALETTE)
    {
        int count = clamp(layer.paletteCount, 1, 4);
        int pick = min(int(Hash12(cellId + layer.seed * 3.7f + 0.5f) * count), count - 1);
        return layer.palette[pick].rgb;
    }
    if (layer.fill == FILL_IMAGE && layer.hasImage != 0)
    {
        float2 imageUv = uv * float2(Aspect(), 1.0f) * max(layer.imageScale, 0.01f);
        if (layer.imageScale <= 0.0f)
        {
            imageUv = uv;
        }
        return ImageValue(index, imageUv).rgb * layer.color.rgb;
    }
    if (layer.fill == FILL_PREVIOUS && gHasSnapshot != 0)
    {
        return gSnapshot.SampleLevel(gClampSampler, uv, 0.0f).rgb;
    }
    return layer.color.rgb;
}

/// 下の画面を崩して読む
float3 SampleScene(float2 uv, int2 pixel)
{
    float aspect = Aspect();
    float2 texel = 1.0f / float2(gTextureSize);
    float2 suv = uv;

    // 揺れ
    if (gShake > 0.0f)
    {
        float step = floor(gTime * 30.0f);
        suv += (float2(Hash12(float2(step, 1.7f)), Hash12(float2(step, 9.3f))) - 0.5f) * 2.0f * gShake * texel;
    }
    // 波打ち
    suv.x += sin(suv.y * 18.0f + gTime * 9.0f) * gWave * texel.x;

    // ズーム・回転・渦（画面の中心のまわり）
    float2 p = (suv - 0.5f) * float2(aspect, 1.0f);
    p /= max(1.0f + gZoom, 0.05f);
    p = Rotate2D(p, -gRotate);
    float r = length(p);
    float swirlRadius = 0.5f * length(float2(aspect, 1.0f));
    if (r < swirlRadius)
    {
        float t = 1.0f - r / swirlRadius;
        p = Rotate2D(p, gSwirl * t * t);
    }
    suv = p / float2(aspect, 1.0f) + 0.5f;

    // モザイク
    if (gMosaic >= 1.5f)
    {
        float2 block = gMosaic * texel;
        suv = (floor(suv / block) + 0.5f) * block;
    }

    float3 color = 0.0f;
    if (gBlur > 0.5f || gZoomBlur > 0.0f)
    {
        const int kTaps = 16;
        for (int i = 0; i < kTaps; ++i)
        {
            float2 offset = 0.0f;
            if (gBlur > 0.5f)
            {
                float rr = sqrt((i + 0.5f) / kTaps) * gBlur;
                float a = i * 2.39996323f;
                offset += float2(cos(a), sin(a)) * rr * texel;
            }
            // 中心へ流れるぼかし
            offset += (0.5f - suv) * gZoomBlur * (float(i) / kTaps);
            color += gScene.SampleLevel(gClampSampler, suv + offset, 0.0f).rgb;
        }
        color /= kTaps;
    }
    else
    {
        color = gScene.SampleLevel(gClampSampler, suv, 0.0f).rgb;
    }

    // 色ずれ
    if (gChroma > 0.0f)
    {
        float2 shift = normalize(suv - 0.5f + 1e-5f) * gChroma * texel;
        color.r = gScene.SampleLevel(gClampSampler, suv + shift, 0.0f).r;
        color.b = gScene.SampleLevel(gClampSampler, suv - shift, 0.0f).b;
    }

    float gray = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(color, gray.xxx, saturate(gDesaturate));
    color = (gBrightness >= 0.0f) ? lerp(color, float3(1.0f, 1.0f, 1.0f), saturate(gBrightness))
                                  : lerp(color, float3(0.0f, 0.0f, 0.0f), saturate(-gBrightness));
    return color;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    if (pixel.x >= gTextureSize.x || pixel.y >= gTextureSize.y)
    {
        return;
    }
    float2 uv = (float2(pixel) + 0.5f) / float2(gTextureSize);
    float3 color = SampleScene(uv, pixel);

    [unroll]
    for (int i = 0; i < MAX_LAYERS; ++i)
    {
        if (i >= gLayerCount)
        {
            break;
        }
        Layer layer = gLayers[i];
        if (layer.progress <= 0.0f && layer.edgeWidth <= 0.0f)
        {
            continue;
        }

        float coverage;
        float distanceToEdge;
        float2 cellId;
        if (layer.shape == SHAPE_FADE)
        {
            coverage = layer.progress;
            distanceToEdge = 1.0f; // フェードにはふちが無い
            cellId = floor(uv * 6.0f);
        }
        else
        {
            float v = ShapeValue(layer, i, uv, cellId);
            if (layer.invert != 0)
            {
                v = 1.0f - v;
            }
            float softness = max(layer.softness, 1e-4f);
            // progress=0 で必ず何も覆わず、1 で必ず全部覆うように、ぼかしの幅ぶん伸ばす
            float d = layer.progress * (1.0f + softness) - v;
            coverage = saturate(d / softness);
            distanceToEdge = d - softness * 0.5f;
        }

        float3 fill = FillColor(layer, i, uv, cellId);
        color = lerp(color, fill, coverage * layer.opacity);

        // ふちの光（覆い始めから覆いきるまでの間だけ）
        if (layer.edgeWidth > 0.0f && layer.progress > 0.0f && layer.progress < 1.0f)
        {
            float glow = exp(-pow(distanceToEdge / layer.edgeWidth, 2.0f));
            color += layer.edgeColor.rgb * layer.edgeColor.a * glow * layer.opacity;
        }
    }

    gOutput[pixel] = float4(color, 1.0f);
}
