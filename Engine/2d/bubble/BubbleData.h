#pragma once
#include "Easing.h"
#include <nlohmann/json.hpp>
#include <string>
#include <type/Vector2.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <vector>

namespace Hagine {

/// <summary>
/// 文字の折り返し方
/// </summary>
enum class BubbleWrapMode
{
    Width,       // 幅で折り返す
    NewlineOnly, // 改行だけ
    Count,
};

/// <summary>
/// 行の揃え
/// </summary>
enum class BubbleTextAlign
{
    Left,
    Center,
    Right,
    Count,
};

/// <summary>
/// 文字送りで1文字が出るときの動き
/// </summary>
enum class BubbleCharAppear
{
    Instant, // そのまま
    Fade,    // 薄い所から
    Pop,     // 弾んで大きく
    Drop,    // 上から落ちる
    Rise,    // 下から浮く
    Slide,   // 右から滑り込む
    Stamp,   // 大きい所から縮む
    Spin,    // 回りながら出る
    Jitter,  // 震えながら止まる
    Garble,  // 文字化けから戻る
    Count,
};

/// <summary>
/// 吹き出し全体の出方と消え方
/// </summary>
enum class BubbleAppear
{
    None,       // なし
    Fade,       // 薄い所から
    Pop,        // 基準点から広がる
    Rise,       // 下から浮く
    Drop,       // 上から落ちる
    SlideLeft,  // 左から滑り込む
    SlideRight, // 右から滑り込む
    StretchX,   // 横に伸びる
    StretchY,   // 縦に伸びる
    Spin,       // 回りながら広がる
    Stamp,      // 大きい所から縮む
    Count,
};

/// <summary>
/// 9スライスの辺の伸ばし方
/// </summary>
enum class BubbleEdgeMode
{
    Stretch, // 伸ばす
    Tile,    // 繰り返す
    Count,
};

/// <summary>
/// 表示中ずっと続く文字と吹き出し全体の動き（0 で無し）
/// </summary>
struct BubbleMotion
{
    float textShake = 0.0f;  // 文字の震え [px]
    float textWave = 0.0f;   // 文字の波 [px]
    float textSway = 0.0f;   // 文字の揺れ [度]
    float textGlitch = 0.0f; // 文字化けの頻度（0〜1）
    float bodyShake = 0.0f;  // 全体の震え [px]
    float bodySway = 0.0f;   // 全体の揺れ [度]
    float speed = 1.0f;      // 動きの速さの倍率
};

/// <summary>
/// 吹き出しの追従先の種類
/// </summary>
enum class BubbleTargetKind
{
    Screen, // 画面上の位置
    Object, // オブジェクト
    Sprite, // スプライト
    Count,
};

/// <summary>
/// 吹き出しの見た目（本体・しっぽ・文字・大きさ・折り返し）
/// </summary>
struct BubbleStyle
{
    std::string name = "NewStyle";

    // ---- 本体 ----
    std::string bodyTexture = "debug/bubble_body.png";  // 本体の画像（images からの相対パス）
    Vector4 sliceBorder = {24.0f, 24.0f, 24.0f, 24.0f}; // 9スライスの固定幅（左, 上, 右, 下）[px]
    BubbleEdgeMode edgeMode = BubbleEdgeMode::Stretch;  // 辺の伸ばし方
    Vector4 bodyColor = {1.0f, 1.0f, 1.0f, 1.0f};       // 本体に掛ける色

    // ---- しっぽ ----
    std::string tailTexture = "debug/bubble_tail.png"; // しっぽの画像（images からの相対パス）
    Vector2 tailPivot = {20.0f, 0.0f};                 // しっぽの付け根（画像内の px）
    float tailOverlap = 6.0f;                          // 本体の内側へ重ねる量 [px]
    float cornerRadius = 18.0f;                        // しっぽが角を回るときの丸み [px]

    // ---- 文字 ----
    Vector4 textPadding = {24.0f, 20.0f, 24.0f, 20.0f}; // 本体の内側と文字の間（左, 上, 右, 下）[px]
    std::string font = "NotoSansJP-Medium.ttf";       // フォント（fonts からの相対パス）
    float fontSize = 32.0f;                           // 文字の大きさ [px]
    Vector4 textColor = {0.1f, 0.1f, 0.1f, 1.0f};     // 文字の色
    float lineSpacing = 1.25f;                        // 行の間隔（文字の大きさの倍率）
    float letterSpacing = 0.0f;                       // 字間 [px]
    BubbleTextAlign align = BubbleTextAlign::Left;    // 行の揃え

    // ---- 大きさ ----
    float minWidth = 120.0f; // 本体の最小幅 [px]
    float maxWidth = 520.0f; // 本体の最大幅 [px]

    // ---- 折り返し ----
    BubbleWrapMode wrapMode = BubbleWrapMode::Width; // 折り返し方
    float wrapWidth = 0.0f;                          // 折り返す幅（0 で最大幅から余白を引いた幅）[px]
    bool kinsoku = true;                             // 句読点・閉じ括弧を行頭に置かない
};

/// <summary>
/// 吹き出し1つ分の出し方（文字・追従先・しっぽの位置・出方・文字送り）
/// </summary>
struct BubbleInstance
{
    std::string name = "NewBubble";
    std::string style;  // 使うスタイルの名前
    std::string text;   // 表示する文字列

    // ---- 位置 ----
    BubbleTargetKind targetKind = BubbleTargetKind::Screen; // 追従先の種類
    std::string target;                          // 追従先の名前（オブジェクト名・スプライト名）
    Vector3 worldOffset = {0.0f, 0.0f, 0.0f};    // 追従先からのずれ（ワールド座標）
    Vector2 screenPosition = {640.0f, 300.0f};   // 画面上の位置（追従先が画面のとき）[px]
    Vector2 offset = {0.0f, -24.0f};             // 画面上のずれ [px]
    Vector2 pivot = {0.5f, 1.0f};                // 本体の基準点（0〜1）

    // ---- しっぽ ----
    bool showTail = true;       // しっぽを出す
    float tailPosition = 0.6f;  // 本体の周りの位置（0〜1、上辺の左端から時計回り）
    bool tailFlip = false;      // しっぽを左右反転

    // ---- 出方・消え方 ----
    BubbleAppear appear = BubbleAppear::Pop;       // 出方と消え方
    float appearDuration = 0.25f;                  // 出る時間 [秒]
    EasingType appearEasing = EasingType::OutBack; // 出るときのイージング
    float disappearDuration = 0.15f;               // 消える時間 [秒]

    // ---- 文字送り ----
    float charInterval = 0.05f;                       // 1文字の間隔（0 で一括）[秒]
    float punctuationPause = 0.2f;                    // 句読点の後に足す待ち [秒]
    BubbleCharAppear charAppear = BubbleCharAppear::Fade; // 1文字の出方
    float charDuration = 0.15f;                       // 1文字が出きるまでの時間 [秒]

    // ---- 表示中の動き ----
    BubbleMotion motion; // 文字と全体の動き

    // ---- 消えるまで ----
    float lifeTime = -1.0f; // 全文が出てから消えるまで（負で消さない）[秒]
};

/// <summary>
/// 吹き出しのスタイルと出し方の一覧（jsons/Bubbles/Bubbles.json に保存）
/// </summary>
class BubbleLibrary
{
  public:
    /// <summary>
    /// 保存先のパス
    /// </summary>
    static std::string FilePath();

    /// <summary>
    /// ファイルから読み込み（無ければひな形を作成）
    /// </summary>
    void Load();

    /// <summary>
    /// スタイルのひな形の一覧
    /// </summary>
    static std::vector<BubbleStyle> BuiltInStyles();

    /// <summary>
    /// 出し方のひな形の一覧
    /// </summary>
    static std::vector<BubbleInstance> BuiltInInstances();

    /// <summary>
    /// ファイルへ保存
    /// </summary>
    /// <returns>bool: 書けたら true</returns>
    bool Save() const;

    /// <summary>
    /// 名前でスタイルを検索
    /// </summary>
    const BubbleStyle *FindStyle(const std::string &name) const;
    BubbleStyle *FindStyle(const std::string &name);

    /// <summary>
    /// 名前で出し方を検索
    /// </summary>
    const BubbleInstance *FindInstance(const std::string &name) const;
    BubbleInstance *FindInstance(const std::string &name);

    /// <summary>
    /// 重ならないスタイル名を作成（"Name2" の形）
    /// </summary>
    std::string MakeUniqueStyleName(const std::string &base) const;

    /// <summary>
    /// 重ならない出し方の名前を作成（"Name2" の形）
    /// </summary>
    std::string MakeUniqueInstanceName(const std::string &base) const;

    std::vector<BubbleStyle> styles;       // スタイルの一覧
    std::vector<BubbleInstance> instances; // 出し方の一覧
};

// JSON との変換
void to_json(nlohmann::json &json, const BubbleStyle &style);
void from_json(const nlohmann::json &json, BubbleStyle &style);
void to_json(nlohmann::json &json, const BubbleInstance &instance);
void from_json(const nlohmann::json &json, BubbleInstance &instance);

} // namespace Hagine
