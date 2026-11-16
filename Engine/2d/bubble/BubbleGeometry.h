#pragma once
#include "BubbleData.h"
#include "BubbleText.h"
#include <vector>

namespace Hagine {

/// <summary>
/// 吹き出しの四角形1枚（頂点は左上・右上・右下・左下の順）
/// </summary>
struct BubbleQuad
{
    Vector2 position[4] = {}; // 画面上の位置 [px]
    Vector2 uv[4] = {};       // 画像内の位置
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 掛ける色
};

/// <summary>
/// 吹き出しを描く四角形の一覧
/// </summary>
struct BubbleMesh
{
    std::vector<BubbleQuad> body; // 本体（9スライス）
    std::vector<BubbleQuad> tail; // しっぽ
    std::vector<BubbleQuad> text; // 文字
    Vector2 bodyMin = {};         // 本体の左上 [px]
    Vector2 bodyMax = {};         // 本体の右下 [px]
    Vector2 textMin = {};         // 文字の領域の左上 [px]
    Vector2 textMax = {};         // 文字の領域の右下 [px]
    Vector2 tailPoint = {};       // しっぽの付け根の位置 [px]
};

/// <summary>
/// 吹き出しの形を作るときの位置・しっぽ・動きの指定
/// </summary>
struct BubbleShapeParams
{
    Vector2 anchor = {};                 // 本体の基準点を合わせる位置 [px]
    Vector2 pivot = {0.5f, 1.0f};        // 本体の基準点（0〜1）
    Vector2 bodyTextureSize = {1.0f, 1.0f}; // 本体の画像の大きさ [px]
    Vector2 tailTextureSize = {};        // しっぽの画像の大きさ（0 でしっぽなし）[px]
    bool showTail = true;                // しっぽを出す
    float tailPosition = 0.6f;           // しっぽの位置（0〜1）
    bool tailFlip = false;               // しっぽを左右反転
    bool clampToScreen = false;          // 本体を画面内に収める
    Vector2 screenSize = {};             // 画面の大きさ [px]
    BubbleAppear appear = BubbleAppear::None;  // 出方
    float appearProgress = 1.0f;         // 出方の進み具合（イージング済み）
    BubbleCharAppear charAppear = BubbleCharAppear::Instant; // 1文字の出方
    const std::vector<float> *charProgress = nullptr; // 文字ごとの出る進み具合（nullptr で全部表示）
    const BubbleMotion *motion = nullptr; // 表示中の動き（nullptr で無し）
    float time = 0.0f;                    // 表示してからの時間 [秒]
};

/// <summary>
/// 吹き出しの大きさ・しっぽの位置・描く四角形の算出
/// </summary>
namespace BubbleGeometry {

/// <summary>
/// 文字の量から本体の大きさを算出
/// </summary>
Vector2 BodySize(const BubbleStyle &style, const BubbleTextLayout &text);

/// <summary>
/// しっぽが角を回るときの丸みを算出
/// </summary>
float CornerRadius(const BubbleStyle &style, const Vector2 &bodyMin, const Vector2 &bodyMax);

/// <summary>
/// 本体の周りの位置（0〜1）から点と外向きの向きを算出
/// </summary>
/// <param name="t">上辺の左端から時計回りの位置（下辺の中央が 0.6）</param>
void PerimeterPoint(const Vector2 &bodyMin, const Vector2 &bodyMax, float radius, float t, Vector2 &point, Vector2 &normal);

/// <summary>
/// 指定位置にいちばん近い本体の周りの位置（0〜1）を算出
/// </summary>
float NearestPerimeter(const Vector2 &bodyMin, const Vector2 &bodyMax, float radius, const Vector2 &position);

/// <summary>
/// 本体・しっぽ・文字の四角形を作成
/// </summary>
BubbleMesh Build(const BubbleStyle &style, const BubbleTextLayout &text, const BubbleShapeParams &params);

} // namespace BubbleGeometry

} // namespace Hagine
