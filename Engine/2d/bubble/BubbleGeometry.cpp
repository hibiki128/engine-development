#include "BubbleGeometry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace Hagine {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kEdgeSpan = 0.2f;       // 辺1本ぶんの位置の幅
constexpr float kCornerSpan = 0.05f;    // 角1つぶんの位置の幅
constexpr float kScreenMargin = 8.0f;   // 画面の端との間 [px]
constexpr float kRiseDistance = 24.0f;  // 下から浮くときの距離 [px]
constexpr float kDropDistance = 40.0f;  // 上から落ちるときの距離 [px]
constexpr float kSlideDistance = 64.0f; // 横から滑り込むときの距離 [px]
constexpr float kShakeRate = 24.0f;     // 震えの切り替わり [回/秒]
constexpr float kGlitchRate = 10.0f;    // 文字化けの切り替わり [回/秒]
constexpr float kGarbleRate = 20.0f;    // 文字化けから戻るときの切り替わり [回/秒]

Vector2 Mul(const Vector2 &a, const Vector2 &b) { return {a.x * b.x, a.y * b.y}; }

Vector4 WithAlpha(const Vector4 &color, float alpha) { return {color.x, color.y, color.z, color.w * alpha}; }

float Saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }

float EaseOutQuad(float k) { return ApplyEasing(EasingType::OutQuad, 0.0f, 1.0f, Saturate(k), 1.0f); }

/// <summary>
/// 整数の組から 0〜1 の擬似乱数を算出
/// </summary>
float Hash01(int a, int b, int c)
{
    uint32_t h = static_cast<uint32_t>(a) * 0x8da6b343u ^ static_cast<uint32_t>(b) * 0xd8163841u ^ static_cast<uint32_t>(c) * 0xcb1ab31fu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
}

/// <summary>
/// 両端の範囲に収める（範囲が逆なら下限を優先）
/// </summary>
float ClampLoose(float value, float low, float high)
{
    return (std::max)(low, (std::min)(value, high));
}

/// <summary>
/// 基準点まわりの拡縮・回転と平行移動
/// </summary>
struct Transform2D
{
    Vector2 origin = {};        // 拡縮・回転の中心
    Vector2 scale = {1.0f, 1.0f}; // 拡縮
    float rotation = 0.0f;      // 回転 [ラジアン]
    Vector2 shift = {};         // 平行移動

    Vector2 Apply(const Vector2 &q) const
    {
        const float x = (q.x - origin.x) * scale.x;
        const float y = (q.y - origin.y) * scale.y;
        const float c = std::cos(rotation);
        const float s = std::sin(rotation);
        return {origin.x + x * c - y * s + shift.x, origin.y + x * s + y * c + shift.y};
    }
};

/// <summary>
/// 出方の進み具合から全体の変換と濃さを算出
/// </summary>
void AppearTransform(BubbleAppear appear, float p, Transform2D &transform, float &alpha)
{
    const float inv = 1.0f - p;
    const float grow = (std::max)(p, 0.0f);
    switch (appear)
    {
    case BubbleAppear::Fade:
        alpha = Saturate(p);
        break;
    case BubbleAppear::Pop:
        transform.scale = {grow, grow};
        alpha = Saturate(p * 2.0f);
        break;
    case BubbleAppear::Rise:
        transform.shift.y = inv * kRiseDistance;
        alpha = Saturate(p);
        break;
    case BubbleAppear::Drop:
        transform.shift.y = -inv * kDropDistance;
        alpha = Saturate(p);
        break;
    case BubbleAppear::SlideLeft:
        transform.shift.x = -inv * kSlideDistance;
        alpha = Saturate(p);
        break;
    case BubbleAppear::SlideRight:
        transform.shift.x = inv * kSlideDistance;
        alpha = Saturate(p);
        break;
    case BubbleAppear::StretchX:
        transform.scale.x = grow;
        alpha = Saturate(p * 2.0f);
        break;
    case BubbleAppear::StretchY:
        transform.scale.y = grow;
        alpha = Saturate(p * 2.0f);
        break;
    case BubbleAppear::Spin:
        transform.scale = {grow, grow};
        transform.rotation = -inv * kPi;
        alpha = Saturate(p * 2.0f);
        break;
    case BubbleAppear::Stamp:
    {
        const float scale = 1.0f + inv * 1.2f;
        transform.scale = {scale, scale};
        alpha = Saturate(p);
        break;
    }
    default:
        break;
    }
}

/// <summary>
/// 9スライスの1方向ぶんの区間（位置と画像内の位置）
/// </summary>
struct SliceSpan
{
    float p0; // 始点 [px]
    float p1; // 終点 [px]
    float t0; // 画像内の始点
    float t1; // 画像内の終点
};

/// <summary>
/// 9スライスの1方向ぶんの区間を作成（辺は伸ばすか繰り返す）
/// </summary>
std::vector<SliceSpan> MakeSpans(float start, float end, float borderA, float borderB, float texSize, bool tile)
{
    const float length = end - start;
    float a = (std::max)(borderA, 0.0f);
    float b = (std::max)(borderB, 0.0f);
    if (a + b > length && a + b > 0.0f)
    {
        const float k = length / (a + b);
        a *= k;
        b *= k;
    }
    const float tA = std::clamp(borderA / texSize, 0.0f, 1.0f);
    const float tB = std::clamp(1.0f - borderB / texSize, 0.0f, 1.0f);

    std::vector<SliceSpan> spans;
    spans.push_back({start, start + a, 0.0f, tA});
    const float middle = length - a - b;
    const float texMiddle = texSize - borderA - borderB;
    const int pieces = (tile && texMiddle > 0.5f) ? (std::max)(1, static_cast<int>(std::lround(middle / texMiddle))) : 1;
    for (int i = 0; i < pieces; ++i)
    {
        spans.push_back({start + a + middle * i / pieces, start + a + middle * (i + 1) / pieces, tA, tB});
    }
    spans.push_back({end - b, end, tB, 1.0f});
    return spans;
}

/// <summary>
/// 四角形を1枚追加
/// </summary>
void PushQuad(std::vector<BubbleQuad> &quads, const Vector2 (&position)[4], const Vector2 (&uv)[4], const Vector4 &color)
{
    BubbleQuad quad;
    for (int i = 0; i < 4; ++i)
    {
        quad.position[i] = position[i];
        quad.uv[i] = uv[i];
    }
    quad.color = color;
    quads.push_back(quad);
}

} // namespace

namespace BubbleGeometry {

Vector2 BodySize(const BubbleStyle &style, const BubbleTextLayout &text)
{
    const Vector4 &pad = style.textPadding;
    const Vector4 &border = style.sliceBorder;
    const float minWidth = (std::min)(style.minWidth, style.maxWidth);
    float width = std::clamp(text.size.x + pad.x + pad.z, minWidth, (std::max)(style.maxWidth, minWidth));
    float height = text.size.y + pad.y + pad.w;
    width = (std::max)(width, border.x + border.z);
    height = (std::max)(height, border.y + border.w);
    return {width, height};
}

float CornerRadius(const BubbleStyle &style, const Vector2 &bodyMin, const Vector2 &bodyMax)
{
    const float half = 0.5f * (std::min)(bodyMax.x - bodyMin.x, bodyMax.y - bodyMin.y);
    return std::clamp(style.cornerRadius, 0.0f, (std::max)(half, 0.0f));
}

void PerimeterPoint(const Vector2 &bodyMin, const Vector2 &bodyMax, float radius, float t, Vector2 &point, Vector2 &normal)
{
    t -= std::floor(t);
    const float x0 = bodyMin.x;
    const float y0 = bodyMin.y;
    const float x1 = bodyMax.x;
    const float y1 = bodyMax.y;
    const float r = radius;
    const float innerWidth = (std::max)(x1 - x0 - 2.0f * r, 0.0f);
    const float innerHeight = (std::max)(y1 - y0 - 2.0f * r, 0.0f);

    // 角の円弧上の点
    auto corner = [&](const Vector2 &center, float startAngle, float f) {
        const float angle = startAngle + f * 0.5f * kPi;
        normal = {std::cos(angle), std::sin(angle)};
        point = {center.x + normal.x * r, center.y + normal.y * r};
    };

    const float e = kEdgeSpan;
    const float c = kCornerSpan;
    if (t < e)
    {
        point = {x0 + r + t / e * innerWidth, y0};
        normal = {0.0f, -1.0f};
    }
    else if (t < e + c)
    {
        corner({x1 - r, y0 + r}, -0.5f * kPi, (t - e) / c);
    }
    else if (t < 2.0f * e + c)
    {
        point = {x1, y0 + r + (t - e - c) / e * innerHeight};
        normal = {1.0f, 0.0f};
    }
    else if (t < 2.0f * e + 2.0f * c)
    {
        corner({x1 - r, y1 - r}, 0.0f, (t - 2.0f * e - c) / c);
    }
    else if (t < 3.0f * e + 2.0f * c)
    {
        point = {x1 - r - (t - 2.0f * e - 2.0f * c) / e * innerWidth, y1};
        normal = {0.0f, 1.0f};
    }
    else if (t < 3.0f * e + 3.0f * c)
    {
        corner({x0 + r, y1 - r}, 0.5f * kPi, (t - 3.0f * e - 2.0f * c) / c);
    }
    else if (t < 4.0f * e + 3.0f * c)
    {
        point = {x0, y1 - r - (t - 3.0f * e - 3.0f * c) / e * innerHeight};
        normal = {-1.0f, 0.0f};
    }
    else
    {
        corner({x0 + r, y0 + r}, kPi, (t - 4.0f * e - 3.0f * c) / c);
    }
}

float NearestPerimeter(const Vector2 &bodyMin, const Vector2 &bodyMax, float radius, const Vector2 &position)
{
    constexpr int kSteps = 400;
    float best = 0.0f;
    float bestDistance = (std::numeric_limits<float>::max)();
    for (int i = 0; i < kSteps; ++i)
    {
        const float t = static_cast<float>(i) / kSteps;
        Vector2 point;
        Vector2 normal;
        PerimeterPoint(bodyMin, bodyMax, radius, t, point, normal);
        const float dx = point.x - position.x;
        const float dy = point.y - position.y;
        const float distance = dx * dx + dy * dy;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = t;
        }
    }
    return best;
}

BubbleMesh Build(const BubbleStyle &style, const BubbleTextLayout &text, const BubbleShapeParams &params)
{
    BubbleMesh mesh;
    const Vector2 size = BodySize(style, text);
    const BubbleMotion motion = params.motion ? *params.motion : BubbleMotion{};
    const float speed = (std::max)(motion.speed, 0.0f);
    const float time = params.time;

    // 本体の位置
    Vector2 bodyMin = params.anchor - Mul(params.pivot, size);
    if (params.clampToScreen)
    {
        bodyMin.x = ClampLoose(bodyMin.x, kScreenMargin, params.screenSize.x - kScreenMargin - size.x);
        bodyMin.y = ClampLoose(bodyMin.y, kScreenMargin, params.screenSize.y - kScreenMargin - size.y);
    }
    const Vector2 bodyMax = bodyMin + size;
    mesh.bodyMin = bodyMin;
    mesh.bodyMax = bodyMax;

    // 出方と全体の動き
    Transform2D transform;
    transform.origin = bodyMin + Mul(params.pivot, size);
    float alpha = 1.0f;
    AppearTransform(params.appear, params.appearProgress, transform, alpha);
    if (motion.bodyShake > 0.0f)
    {
        const int step = static_cast<int>(std::floor(time * kShakeRate * speed));
        transform.shift.x += (Hash01(-1, step, 0) - 0.5f) * 2.0f * motion.bodyShake;
        transform.shift.y += (Hash01(-1, step, 1) - 0.5f) * 2.0f * motion.bodyShake;
    }
    if (motion.bodySway > 0.0f)
    {
        transform.rotation += std::sin(time * speed * 2.0f * kPi * 0.5f) * motion.bodySway * kDegToRad;
    }

    // 本体（9スライス）
    {
        const Vector2 tex = {(std::max)(params.bodyTextureSize.x, 1.0f), (std::max)(params.bodyTextureSize.y, 1.0f)};
        const bool tile = style.edgeMode == BubbleEdgeMode::Tile;
        const std::vector<SliceSpan> columns = MakeSpans(bodyMin.x, bodyMax.x, style.sliceBorder.x, style.sliceBorder.z, tex.x, tile);
        const std::vector<SliceSpan> rows = MakeSpans(bodyMin.y, bodyMax.y, style.sliceBorder.y, style.sliceBorder.w, tex.y, tile);
        const Vector4 color = WithAlpha(style.bodyColor, alpha);
        for (const SliceSpan &row : rows)
        {
            for (const SliceSpan &col : columns)
            {
                if (col.p1 - col.p0 <= 0.0f || row.p1 - row.p0 <= 0.0f)
                {
                    continue;
                }
                const Vector2 position[4] = {
                    transform.Apply({col.p0, row.p0}), transform.Apply({col.p1, row.p0}),
                    transform.Apply({col.p1, row.p1}), transform.Apply({col.p0, row.p1})};
                const Vector2 uv[4] = {{col.t0, row.t0}, {col.t1, row.t0}, {col.t1, row.t1}, {col.t0, row.t1}};
                PushQuad(mesh.body, position, uv, color);
            }
        }
    }

    // しっぽ
    {
        Vector2 point;
        Vector2 normal;
        PerimeterPoint(bodyMin, bodyMax, CornerRadius(style, bodyMin, bodyMax), params.tailPosition, point, normal);
        mesh.tailPoint = transform.Apply(point);
        const Vector2 tex = params.tailTextureSize;
        if (params.showTail && tex.x > 0.0f && tex.y > 0.0f)
        {
            const Vector2 root = point - normal * style.tailOverlap;
            const float angle = std::atan2(normal.y, normal.x) - 0.5f * kPi;
            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);
            const float flip = params.tailFlip ? -1.0f : 1.0f;
            const float cornerU[4] = {0.0f, 1.0f, 1.0f, 0.0f};
            const float cornerV[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            Vector2 position[4];
            Vector2 uv[4];
            for (int i = 0; i < 4; ++i)
            {
                const float lx = (cornerU[i] * tex.x - style.tailPivot.x) * flip;
                const float ly = cornerV[i] * tex.y - style.tailPivot.y;
                position[i] = transform.Apply({root.x + lx * cosA - ly * sinA, root.y + lx * sinA + ly * cosA});
                uv[i] = {cornerU[i], cornerV[i]};
            }
            PushQuad(mesh.tail, position, uv, WithAlpha(style.bodyColor, alpha));
        }
    }

    // 文字
    {
        const Vector4 &pad = style.textPadding;
        const Vector2 areaMin = {bodyMin.x + pad.x, bodyMin.y + pad.y};
        const Vector2 areaSize = {size.x - pad.x - pad.z, size.y - pad.y - pad.w};
        mesh.textMin = areaMin;
        mesh.textMax = areaMin + areaSize;
        const float top = areaMin.y + (std::max)(0.0f, 0.5f * (areaSize.y - text.size.y));
        const float alignFactor = style.align == BubbleTextAlign::Center ? 0.5f
                                  : style.align == BubbleTextAlign::Right ? 1.0f
                                                                          : 0.0f;
        const float fontSize = text.fontSize;
        const int garbleCount = static_cast<int>(text.garbleGlyphs.size());
        const int shakeStep = static_cast<int>(std::floor(time * kShakeRate * speed));
        const int glitchStep = static_cast<int>(std::floor(time * kGlitchRate * speed));
        const int garbleStep = static_cast<int>(std::floor(time * kGarbleRate));
        for (const BubbleGlyph &glyph : text.glyphs)
        {
            const int ci = glyph.charIndex;
            float k = 1.0f;
            if (params.charProgress)
            {
                const std::vector<float> &progress = *params.charProgress;
                k = ci < static_cast<int>(progress.size()) ? progress[static_cast<size_t>(ci)] : 1.0f;
            }
            if (k <= 0.0f)
            {
                continue;
            }

            // 1文字の出方
            float charAlpha = 1.0f;
            float charScale = 1.0f;
            float charRotation = 0.0f;
            Vector2 offset = {0.0f, 0.0f};
            bool garble = false;
            int garbleSeed = 0;
            switch (params.charAppear)
            {
            case BubbleCharAppear::Fade:
                charAlpha = k;
                break;
            case BubbleCharAppear::Pop:
                charScale = ApplyEasing(EasingType::OutBack, 0.0f, 1.0f, k, 1.0f);
                charAlpha = Saturate(k * 2.0f);
                break;
            case BubbleCharAppear::Drop:
                offset.y = -(1.0f - EaseOutQuad(k)) * fontSize * 0.5f;
                charAlpha = k;
                break;
            case BubbleCharAppear::Rise:
                offset.y = (1.0f - EaseOutQuad(k)) * fontSize * 0.5f;
                charAlpha = k;
                break;
            case BubbleCharAppear::Slide:
                offset.x = (1.0f - EaseOutQuad(k)) * fontSize * 0.8f;
                charAlpha = k;
                break;
            case BubbleCharAppear::Stamp:
                charScale = 1.0f + (1.0f - EaseOutQuad(k)) * 1.5f;
                charAlpha = Saturate(k * 3.0f);
                break;
            case BubbleCharAppear::Spin:
                charRotation = -(1.0f - EaseOutQuad(k)) * kPi;
                charScale = EaseOutQuad(k);
                charAlpha = k;
                break;
            case BubbleCharAppear::Jitter:
            {
                const float amplitude = (1.0f - k) * fontSize * 0.25f;
                offset.x = (Hash01(ci, shakeStep, 6) - 0.5f) * 2.0f * amplitude;
                offset.y = (Hash01(ci, shakeStep, 7) - 0.5f) * 2.0f * amplitude;
                charAlpha = Saturate(k * 2.0f);
                break;
            }
            case BubbleCharAppear::Garble:
                garble = k < 1.0f;
                garbleSeed = garbleStep;
                break;
            default:
                break;
            }

            // 表示中の動き
            if (motion.textShake > 0.0f)
            {
                offset.x += (Hash01(ci, shakeStep, 2) - 0.5f) * 2.0f * motion.textShake;
                offset.y += (Hash01(ci, shakeStep, 3) - 0.5f) * 2.0f * motion.textShake;
            }
            if (motion.textWave > 0.0f)
            {
                offset.y += std::sin(time * speed * 2.0f * kPi + ci * 0.6f) * motion.textWave;
            }
            if (motion.textSway > 0.0f)
            {
                charRotation += std::sin(time * speed * 2.0f * kPi * 0.7f + ci * 0.9f) * motion.textSway * kDegToRad;
            }
            if (!garble && motion.textGlitch > 0.0f && Hash01(ci, glitchStep, 4) < motion.textGlitch * 0.35f)
            {
                garble = true;
                garbleSeed = glitchStep;
            }

            // 描く字（文字化け中は別の字）
            const float lineWidth = glyph.line < static_cast<int>(text.lineWidths.size()) ? text.lineWidths[static_cast<size_t>(glyph.line)] : 0.0f;
            const Vector2 lineOrigin = {areaMin.x + (areaSize.x - lineWidth) * alignFactor, top};
            const BubbleGlyph *drawn = &glyph;
            Vector2 lt = lineOrigin + glyph.position;
            if (garble && garbleCount > 0)
            {
                const int index = static_cast<int>(Hash01(ci, garbleSeed, 5) * garbleCount) % garbleCount;
                drawn = &text.garbleGlyphs[static_cast<size_t>(index)];
                lt = lineOrigin + glyph.origin + drawn->position;
            }
            lt = lt + offset;

            // 字の中心まわりで拡縮・回転
            const Vector2 center = lt + drawn->size * 0.5f;
            const Vector2 corners[4] = {lt, {lt.x + drawn->size.x, lt.y}, lt + drawn->size, {lt.x, lt.y + drawn->size.y}};
            const float c = std::cos(charRotation);
            const float s = std::sin(charRotation);
            Vector2 position[4];
            for (int i = 0; i < 4; ++i)
            {
                const Vector2 d = (corners[i] - center) * charScale;
                position[i] = transform.Apply({center.x + d.x * c - d.y * s, center.y + d.x * s + d.y * c});
            }
            const Vector2 uv[4] = {drawn->uvMin, {drawn->uvMax.x, drawn->uvMin.y}, drawn->uvMax, {drawn->uvMin.x, drawn->uvMax.y}};
            PushQuad(mesh.text, position, uv, WithAlpha(style.textColor, charAlpha * alpha));
        }
    }
    return mesh;
}

} // namespace BubbleGeometry

} // namespace Hagine
