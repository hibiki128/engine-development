#include "BubbleText.h"
#include <asset/AssetPath.h>
#include <imgui/imstb_truetype.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_map>

namespace Hagine {
namespace {

constexpr int kGlyphPadding = 2;     // 文字の画像どうしの間 [px]
constexpr int kSheetMaxWidth = 1024; // 文字の画像の最大幅 [px]
constexpr char32_t kGarbleChars[] = U"縺繧繝譁蟄怜喧螟謚樔荳蜈菴逕驕鬮ｮｱｦｧ□■◆※〓"; // 文字化けに使う字

/// <summary>
/// UTF-8 の文字列からパスを作成
/// </summary>
std::filesystem::path Utf8Path(const std::string &text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

/// <summary>
/// UTF-8 をコードポイント列へ変換（CR は除く）
/// </summary>
std::vector<uint32_t> DecodeUtf8(const std::string &text)
{
    std::vector<uint32_t> result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size())
    {
        const uint8_t lead = static_cast<uint8_t>(text[i]);
        uint32_t cp = 0;
        size_t extra = 0;
        if (lead < 0x80)
        {
            cp = lead;
        }
        else if ((lead >> 5) == 0x6)
        {
            cp = lead & 0x1F;
            extra = 1;
        }
        else if ((lead >> 4) == 0xE)
        {
            cp = lead & 0x0F;
            extra = 2;
        }
        else if ((lead >> 3) == 0x1E)
        {
            cp = lead & 0x07;
            extra = 3;
        }
        else
        {
            ++i;
            continue;
        }
        if (extra > 0 && i + extra >= text.size())
        {
            break;
        }
        bool valid = true;
        for (size_t k = 1; k <= extra; ++k)
        {
            const uint8_t next = static_cast<uint8_t>(text[i + k]);
            if ((next & 0xC0) != 0x80)
            {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3F);
        }
        i += valid ? extra + 1 : 1;
        if (valid && cp != '\r')
        {
            result.push_back(cp);
        }
    }
    return result;
}

/// <summary>
/// フォントファイルの読み込み（一度読んだ物は使い回す）
/// </summary>
const std::vector<uint8_t> *LoadFontData(const std::string &file)
{
    static std::unordered_map<std::string, std::vector<uint8_t>> cache;
    auto it = cache.find(file);
    if (it == cache.end())
    {
        std::vector<uint8_t> data;
        std::ifstream stream(Utf8Path(AssetPath::Font(file)), std::ios::binary);
        if (stream)
        {
            data.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        }
        it = cache.emplace(file, std::move(data)).first;
    }
    return it->second.empty() ? nullptr : &it->second;
}

/// <summary>
/// 行頭に置かない文字の判定
/// </summary>
bool IsNoLineStart(uint32_t c)
{
    static const std::u32string kChars =
        U"、。，．・：；？！゛゜ヽヾゝゞ々ー）］｝」』】〕〉》〙〗〟’”｠»…‥〜～"
        U"ぁぃぅぇぉっゃゅょゎゕゖァィゥェォッャュョヮヵヶ"
        U")]},.!?:;";
    return kChars.find(static_cast<char32_t>(c)) != std::u32string::npos;
}

/// <summary>
/// 行末に置かない文字の判定
/// </summary>
bool IsNoLineEnd(uint32_t c)
{
    static const std::u32string kChars = U"（［｛「『【〔〈《〘〖〝‘“｟«([{";
    return kChars.find(static_cast<char32_t>(c)) != std::u32string::npos;
}

/// <summary>
/// 英単語の一部として分けない文字の判定
/// </summary>
bool IsWordChar(uint32_t c)
{
    return c > 0x20 && c < 0x7F;
}

/// <summary>
/// 2文字の間で折り返せるかの判定
/// </summary>
bool CanBreakBetween(uint32_t prev, uint32_t next, bool kinsoku)
{
    if (IsWordChar(prev) && IsWordChar(next))
    {
        return false;
    }
    if (kinsoku && (IsNoLineStart(next) || IsNoLineEnd(prev)))
    {
        return false;
    }
    return true;
}

/// <summary>
/// 画像を持たない空白の判定
/// </summary>
bool IsSpace(uint32_t c)
{
    return c == ' ' || c == '\t' || c == 0x3000;
}

/// <summary>
/// 1行分の文字の範囲
/// </summary>
struct LineRange
{
    size_t begin = 0; // 先頭の文字
    size_t end = 0;   // 末尾の次の文字
};

/// <summary>
/// 文字の画像の置き場所
/// </summary>
struct SheetCell
{
    int x = 0;      // 画像内の左
    int y = 0;      // 画像内の上
    int width = 0;  // 幅
    int height = 0; // 高さ
    int x0 = 0;     // 文字の原点からの左のずれ
    int y0 = 0;     // 基準線からの上のずれ
};

} // namespace

float BubbleText::WrapWidth(const BubbleStyle &style)
{
    const float width = style.wrapWidth > 0.0f ? style.wrapWidth
                                               : style.maxWidth - style.textPadding.x - style.textPadding.z;
    return (std::max)(width, style.fontSize);
}

bool BubbleText::UsesGarble(const BubbleInstance &instance)
{
    return instance.charAppear == BubbleCharAppear::Garble || instance.motion.textGlitch > 0.0f;
}

bool BubbleText::IsPause(uint32_t c)
{
    static const std::u32string kChars = U"、。，．！？…‥!?";
    return kChars.find(static_cast<char32_t>(c)) != std::u32string::npos;
}

std::vector<std::string> BubbleText::FontFiles()
{
    std::vector<std::string> files;
    const std::filesystem::path root = Utf8Path(AssetPath::Font(""));
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
    {
        return files;
    }
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        if (!it->is_regular_file(ec))
        {
            continue;
        }
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        if (ext != ".ttf" && ext != ".otf" && ext != ".ttc")
        {
            continue;
        }
        const std::u8string relative = std::filesystem::relative(it->path(), root, ec).generic_u8string();
        files.emplace_back(relative.begin(), relative.end());
    }
    std::sort(files.begin(), files.end());
    return files;
}

BubbleTextLayout BubbleText::Build(const std::string &text, const BubbleStyle &style, bool withGarble)
{
    BubbleTextLayout layout;
    layout.fontSize = (std::max)(style.fontSize, 1.0f);
    layout.lineHeight = layout.fontSize * (std::max)(style.lineSpacing, 0.1f);

    // フォントの準備
    const std::vector<uint8_t> *fontData = LoadFontData(style.font);
    stbtt_fontinfo font{};
    const bool hasFont = fontData && stbtt_InitFont(&font, fontData->data(), stbtt_GetFontOffsetForIndex(fontData->data(), 0)) != 0;
    float scale = 0.0f;
    float ascent = layout.fontSize;
    float descent = 0.0f;
    if (hasFont)
    {
        scale = stbtt_ScaleForPixelHeight(&font, layout.fontSize);
        int a = 0;
        int d = 0;
        int gap = 0;
        stbtt_GetFontVMetrics(&font, &a, &d, &gap);
        ascent = a * scale;
        descent = d * scale;
    }

    // 文字ごとの送り幅
    const std::vector<uint32_t> codepoints = DecodeUtf8(text);
    const size_t count = codepoints.size();
    std::vector<float> advances(count, 0.0f);
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t c = codepoints[i];
        if (c == '\n')
        {
            continue;
        }
        if (hasFont)
        {
            int advance = 0;
            int bearing = 0;
            stbtt_GetCodepointHMetrics(&font, static_cast<int>(c), &advance, &bearing);
            advances[i] = advance * scale;
            if (i + 1 < count && codepoints[i + 1] != '\n')
            {
                advances[i] += stbtt_GetCodepointKernAdvance(&font, static_cast<int>(c), static_cast<int>(codepoints[i + 1])) * scale;
            }
        }
        else
        {
            advances[i] = layout.fontSize * (c < 0x80 ? 0.5f : 1.0f);
        }
        advances[i] += style.letterSpacing;
    }

    // 折り返し
    const float maxLineWidth = style.wrapMode == BubbleWrapMode::Width ? WrapWidth(style) : (std::numeric_limits<float>::max)();
    std::vector<LineRange> lines;
    {
        size_t lineStart = 0;
        size_t lastBreak = 0;
        float width = 0.0f;
        size_t i = 0;
        while (i < count)
        {
            const uint32_t c = codepoints[i];
            if (c == '\n')
            {
                lines.push_back({lineStart, i});
                ++i;
                lineStart = i;
                lastBreak = lineStart;
                width = 0.0f;
                continue;
            }
            if (i > lineStart && CanBreakBetween(codepoints[i - 1], c, style.kinsoku))
            {
                lastBreak = i;
            }
            if (i > lineStart && !IsSpace(c) && width + advances[i] > maxLineWidth)
            {
                const size_t breakAt = lastBreak > lineStart ? lastBreak : i;
                lines.push_back({lineStart, breakAt});
                lineStart = breakAt;
                while (lineStart < count && IsSpace(codepoints[lineStart]) && codepoints[lineStart] != 0x3000)
                {
                    ++lineStart;
                }
                lastBreak = lineStart;
                width = 0.0f;
                i = lineStart;
                continue;
            }
            width += advances[i];
            ++i;
        }
        lines.push_back({lineStart, count});
    }

    // 行の幅（末尾の空白を除く）
    layout.lineWidths.resize(lines.size(), 0.0f);
    for (size_t l = 0; l < lines.size(); ++l)
    {
        size_t end = lines[l].end;
        while (end > lines[l].begin && IsSpace(codepoints[end - 1]))
        {
            --end;
        }
        float width = 0.0f;
        for (size_t i = lines[l].begin; i < end; ++i)
        {
            width += advances[i];
        }
        layout.lineWidths[l] = width;
        layout.size.x = (std::max)(layout.size.x, width);
    }
    layout.size.y = static_cast<float>(lines.size() - 1) * layout.lineHeight + (ascent - descent);

    // 文字の画像の置き場所（同じ文字は1つにまとめる）
    std::unordered_map<uint32_t, SheetCell> cells;
    std::vector<uint32_t> cellOrder;
    auto addCell = [&](uint32_t c) {
        if (!hasFont || IsSpace(c) || cells.contains(c))
        {
            return;
        }
        SheetCell cell;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(&font, static_cast<int>(c), scale, scale, &cell.x0, &cell.y0, &x1, &y1);
        cell.width = x1 - cell.x0;
        cell.height = y1 - cell.y0;
        if (cell.width <= 0 || cell.height <= 0)
        {
            return;
        }
        cells.emplace(c, cell);
        cellOrder.push_back(c);
    };
    for (const LineRange &line : lines)
    {
        for (size_t i = line.begin; i < line.end; ++i)
        {
            addCell(codepoints[i]);
        }
    }
    if (withGarble)
    {
        for (const char32_t *p = kGarbleChars; *p; ++p)
        {
            addCell(static_cast<uint32_t>(*p));
        }
    }

    // 文字の画像を詰めて並べる
    int sheetWidth = kGlyphPadding;
    for (uint32_t c : cellOrder)
    {
        sheetWidth += cells[c].width + kGlyphPadding;
    }
    sheetWidth = std::clamp(sheetWidth, 1, kSheetMaxWidth);
    {
        int x = kGlyphPadding;
        int y = kGlyphPadding;
        int rowHeight = 0;
        for (uint32_t c : cellOrder)
        {
            SheetCell &cell = cells[c];
            if (x + cell.width + kGlyphPadding > sheetWidth && x > kGlyphPadding)
            {
                x = kGlyphPadding;
                y += rowHeight + kGlyphPadding;
                rowHeight = 0;
            }
            cell.x = x;
            cell.y = y;
            x += cell.width + kGlyphPadding;
            rowHeight = (std::max)(rowHeight, cell.height);
        }
        layout.imageWidth = sheetWidth;
        layout.imageHeight = (std::max)(y + rowHeight + kGlyphPadding, 1);
    }
    layout.pixels.assign(static_cast<size_t>(layout.imageWidth) * layout.imageHeight * 4, 0);
    for (size_t p = 0; p < layout.pixels.size(); p += 4)
    {
        layout.pixels[p + 0] = 255;
        layout.pixels[p + 1] = 255;
        layout.pixels[p + 2] = 255;
    }
    std::vector<uint8_t> bitmap;
    for (uint32_t c : cellOrder)
    {
        const SheetCell &cell = cells[c];
        bitmap.assign(static_cast<size_t>(cell.width) * cell.height, 0);
        stbtt_MakeCodepointBitmap(&font, bitmap.data(), cell.width, cell.height, cell.width, scale, scale, static_cast<int>(c));
        for (int gy = 0; gy < cell.height; ++gy)
        {
            for (int gx = 0; gx < cell.width; ++gx)
            {
                const size_t dst = (static_cast<size_t>(cell.y + gy) * layout.imageWidth + (cell.x + gx)) * 4;
                layout.pixels[dst + 3] = bitmap[static_cast<size_t>(gy) * cell.width + gx];
            }
        }
    }

    // 文字の配置と文字送りの順番
    const float invWidth = 1.0f / static_cast<float>(layout.imageWidth);
    const float invHeight = 1.0f / static_cast<float>(layout.imageHeight);
    for (size_t l = 0; l < lines.size(); ++l)
    {
        float penX = 0.0f;
        const float baseline = static_cast<float>(l) * layout.lineHeight + ascent;
        for (size_t i = lines[l].begin; i < lines[l].end; ++i)
        {
            const uint32_t c = codepoints[i];
            const int charIndex = static_cast<int>(layout.chars.size());
            layout.chars.push_back(c);
            auto it = cells.find(c);
            if (it != cells.end())
            {
                const SheetCell &cell = it->second;
                BubbleGlyph glyph;
                glyph.charIndex = charIndex;
                glyph.line = static_cast<int>(l);
                glyph.origin = {std::round(penX), std::round(baseline)};
                glyph.position = {glyph.origin.x + cell.x0, glyph.origin.y + cell.y0};
                glyph.size = {static_cast<float>(cell.width), static_cast<float>(cell.height)};
                glyph.uvMin = {cell.x * invWidth, cell.y * invHeight};
                glyph.uvMax = {(cell.x + cell.width) * invWidth, (cell.y + cell.height) * invHeight};
                layout.glyphs.push_back(glyph);
            }
            penX += advances[i];
        }
    }

    // 文字化けに使う字（位置は原点からのずれ）
    if (withGarble)
    {
        for (const char32_t *p = kGarbleChars; *p; ++p)
        {
            auto it = cells.find(static_cast<uint32_t>(*p));
            if (it == cells.end())
            {
                continue;
            }
            const SheetCell &cell = it->second;
            BubbleGlyph glyph;
            glyph.position = {static_cast<float>(cell.x0), static_cast<float>(cell.y0)};
            glyph.size = {static_cast<float>(cell.width), static_cast<float>(cell.height)};
            glyph.uvMin = {cell.x * invWidth, cell.y * invHeight};
            glyph.uvMax = {(cell.x + cell.width) * invWidth, (cell.y + cell.height) * invHeight};
            layout.garbleGlyphs.push_back(glyph);
        }
    }
    return layout;
}

} // namespace Hagine
