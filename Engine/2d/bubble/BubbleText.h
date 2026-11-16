#pragma once
#include "BubbleData.h"
#include <cstdint>
#include <string>
#include <type/Vector2.h>
#include <vector>

namespace Hagine {

/// <summary>
/// 吹き出しの文字1つ分の配置と、文字の画像内の範囲
/// </summary>
struct BubbleGlyph
{
    int charIndex = 0;       // 文字送りの順番
    int line = 0;            // 行番号
    Vector2 origin = {};     // 文字の原点（送りの位置と基準線）[px]
    Vector2 position = {};   // 行の左上からの位置（文字化けの字では原点からのずれ）[px]
    Vector2 size = {};       // 大きさ [px]
    Vector2 uvMin = {};      // 文字の画像内の左上
    Vector2 uvMax = {};      // 文字の画像内の右下
};

/// <summary>
/// 折り返し済みの文字の配置と、文字を並べた画像
/// </summary>
struct BubbleTextLayout
{
    std::vector<BubbleGlyph> glyphs;  // 画像のある文字
    std::vector<BubbleGlyph> garbleGlyphs; // 文字化けに使う字
    std::vector<float> lineWidths;    // 行ごとの幅 [px]
    std::vector<uint32_t> chars;      // 文字送りの順の文字
    float lineHeight = 0.0f;          // 行の間隔 [px]
    float fontSize = 0.0f;            // 文字の大きさ [px]
    Vector2 size = {};                // 文字の領域の大きさ [px]
    std::vector<uint8_t> pixels;      // 文字を並べた画像（RGBA）
    int imageWidth = 0;               // 画像の幅
    int imageHeight = 0;              // 画像の高さ
};

/// <summary>
/// 吹き出しの文字の折り返し・配置・画像の作成
/// </summary>
class BubbleText
{
  public:
    /// <summary>
    /// 文字列を折り返して配置し、文字の画像を作成
    /// </summary>
    /// <param name="withGarble">文字化けに使う字も画像に含める</param>
    static BubbleTextLayout Build(const std::string &text, const BubbleStyle &style, bool withGarble = false);

    /// <summary>
    /// 出し方が文字化けの字を使うかの判定
    /// </summary>
    static bool UsesGarble(const BubbleInstance &instance);

    /// <summary>
    /// 実際に折り返す幅を算出
    /// </summary>
    static float WrapWidth(const BubbleStyle &style);

    /// <summary>
    /// 文字送りで後に待ちを足す句読点かの判定
    /// </summary>
    static bool IsPause(uint32_t c);

    /// <summary>
    /// fonts 以下のフォントファイルの一覧（fonts からの相対パス）
    /// </summary>
    static std::vector<std::string> FontFiles();
};

} // namespace Hagine
