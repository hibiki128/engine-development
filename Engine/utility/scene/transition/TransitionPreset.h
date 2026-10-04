#pragma once
#include "Easing.h"
#include <type/Vector2.h>
#include <type/Vector4.h>
#include <array>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 幕の形（どの順番で画面が覆われるか）。
/// Transition.CS.hlsl の SHAPE_* と同じ並びにすること。足すときは Count の手前へ
/// </summary>
enum class TransitionShape
{
    Fade,      // 全体が一度に
    Wipe,      // 一方向から
    Iris,      // 円
    Box,       // 四角
    Diamond,   // ひし形
    Clock,     // 時計の針
    Blinds,    // ブラインド
    Bars,      // 順に滑り込む帯
    Split,     // 両側から（扉）
    Tiles,     // 四角いマス
    Hexagons,  // 六角形のマス
    Triangles, // 三角形のマス
    Dots,      // 丸いマス
    Dissolve,  // ノイズで溶ける
    Spiral,    // うずまき
    Star,      // 星形
    Heart,     // ハート
    Wave,      // 波打つワイプ
    Fan,       // 扇
    RuleImage, // ルール画像（明るさの順）
    Count,
};

/// <summary>
/// 幕の塗り方。Transition.CS.hlsl の FILL_* と同じ並び
/// </summary>
enum class TransitionFill
{
    Color,          // 単色
    Gradient,       // 2色のグラデーション
    Palette,        // マスごとに色を塗り分ける
    Image,          // 画像
    PreviousScreen, // 切り替える直前の画面
    Count,
};

/// <summary>
/// マスの埋まる順番。Transition.CS.hlsl の ORDER_* と同じ並び
/// </summary>
enum class TransitionOrder
{
    Random,    // ばらばら
    Sweep,     // 向きに沿って順に
    Center,    // 中心から
    Alternate, // 交互（市松・1本おき）
    Count,
};

/// <summary>
/// 明けるとき（後半）の動き方
/// </summary>
enum class TransitionRevealMode
{
    Reverse,     // 覆ったときの逆再生
    PassThrough, // 同じ向きへ抜ける（覆ったのと同じ順に明ける）
    Custom,      // 明ける用のレイヤーを別に作る
    Count,
};

/// <summary>
/// 幕の1枚。形・塗り・ふちの光と、前半/後半の中でいつ動くかを持つ
/// </summary>
struct TransitionLayer
{
    bool enabled = true;
    std::string name = "幕";

    // ---- 動くタイミング（前半/後半の時間を 0〜1 とした区間）----
    float start = 0.0f;
    float end = 1.0f;
    EasingType easing = EasingType::InOutSine;

    // ---- 形 ----
    TransitionShape shape = TransitionShape::Fade;
    float softness = 0.05f;   // 境目のぼかし
    bool invert = false;      // 覆う順番を逆にする
    float angle = 0.0f;       // 向き（度）
    Vector2 center = {0.5f, 0.5f};
    float count = 8.0f;       // 帯・マス・とげ・巻き数など
    TransitionOrder order = TransitionOrder::Random;
    float cellSpread = 0.6f;  // マスの埋まり始めのばらつき（0で一斉に）
    float amplitude = 0.05f;  // 波の大きさ
    float seed = 0.0f;        // 乱数の種
    std::string ruleImage;    // ルール画像（images からの相対パス）

    // ---- 塗り ----
    TransitionFill fill = TransitionFill::Color;
    Vector4 color = {0.0f, 0.0f, 0.0f, 1.0f};
    Vector4 color2 = {1.0f, 1.0f, 1.0f, 1.0f};
    float gradientAngle = 90.0f;
    std::array<Vector4, 4> palette = {Vector4{0.90f, 0.25f, 0.25f, 1.0f}, Vector4{0.25f, 0.50f, 0.95f, 1.0f},
                                      Vector4{0.30f, 0.80f, 0.40f, 1.0f}, Vector4{0.95f, 0.85f, 0.30f, 1.0f}};
    int paletteCount = 4;
    std::string image;        // 塗りの画像（images からの相対パス）
    float imageScale = 0.0f;  // 画像の繰り返し（0で画面に引き伸ばす）

    // ---- ふちの光 ----
    float edgeWidth = 0.0f;
    Vector4 edgeColor = {1.0f, 0.8f, 0.4f, 1.0f};
    float edgeIntensity = 1.5f;

    float opacity = 1.0f;
};

/// <summary>
/// 下の画面の崩し方。前半は覆う進み具合に合わせて強くなり、後半は弱まっていく
/// </summary>
struct TransitionSceneFx
{
    float mosaic = 0.0f;     // モザイクの大きさ（ピクセル）
    float blur = 0.0f;       // ぼかし（ピクセル）
    float swirl = 0.0f;      // 渦（度）
    float zoom = 0.0f;       // ズーム（+で寄る / -で引く）
    float zoomBlur = 0.0f;   // 中心へ流れるぼかし
    float rotate = 0.0f;     // 回転（度）
    float chroma = 0.0f;     // 色ずれ（ピクセル）
    float desaturate = 0.0f; // 色を抜く
    float brightness = 0.0f; // +で白へ / -で黒へ
    float shake = 0.0f;      // 揺れ（ピクセル）
    float wave = 0.0f;       // 波打ち（ピクセル）
    EasingType easing = EasingType::InQuad;
};

/// <summary>
/// 前半（覆う）または後半（明ける）の1区間
/// </summary>
struct TransitionPhase
{
    float duration = 0.5f;
    std::vector<TransitionLayer> layers;
    TransitionSceneFx sceneFx;
};

/// <summary>
/// シーン遷移の演出1つぶん。
/// 前半で画面を覆い、覆いきったところでシーンが切り替わり、少し待ってから後半で明ける
/// </summary>
struct TransitionPreset
{
    static constexpr int kMaxLayers = 4; // Transition.CS.hlsl の MAX_LAYERS と一致させること

    std::string name = "新しい遷移";
    std::string description;
    TransitionPhase cover;  // 前半（覆う）
    float hold = 0.1f;      // 覆いきってから明けるまで待つ時間（この間にシーンが切り替わる）
    TransitionRevealMode revealMode = TransitionRevealMode::Reverse;
    TransitionPhase reveal; // 後半（明ける）。Reverse / PassThrough のときは時間だけ使う

    /// <summary>後半に使うレイヤー（明け方に応じて前半のものを使い回す）</summary>
    const std::vector<TransitionLayer> &RevealLayers() const
    {
        return revealMode == TransitionRevealMode::Custom ? reveal.layers : cover.layers;
    }
    /// <summary>後半に使う画面の崩し方</summary>
    const TransitionSceneFx &RevealSceneFx() const
    {
        return revealMode == TransitionRevealMode::Custom ? reveal.sceneFx : cover.sceneFx;
    }
    /// <summary>前半＋待ち＋後半の合計時間</summary>
    float TotalDuration() const { return cover.duration + hold + reveal.duration; }
};

/// <summary>
/// どのシーンからどのシーンへ切り替えるときに、どの演出を使うか（"*" はどれでも）
/// </summary>
struct TransitionRule
{
    std::string from = "*";
    std::string to = "*";
    std::string preset;
};

/// <summary>
/// 遷移の演出の一覧と、使い分けの決まり。JSON（jsons/Transitions/Transitions.json）に保存する
/// </summary>
class TransitionLibrary
{
  public:
    /// <summary>保存先のパス</summary>
    static std::string FilePath();

    /// <summary>ファイルから読み込む。無ければひな形から作る</summary>
    void Load();

    /// <summary>ファイルへ保存する</summary>
    /// <returns>bool: 書けたら true</returns>
    bool Save() const;

    /// <summary>ひな形（組み込みの演出）の一覧</summary>
    static std::vector<TransitionPreset> BuiltInPresets();

    /// <summary>名前で演出を探す</summary>
    const TransitionPreset *Find(const std::string &name) const;
    TransitionPreset *Find(const std::string &name);

    /// <summary>
    /// 使う演出を決める。明示された名前 → 決まり（上から順に最初に合った物） → 既定 の順に探す
    /// </summary>
    /// <param name="explicitName">呼び出し側が指定した名前（空なら指定なし）</param>
    /// <param name="fromScene">切り替える前のシーン名</param>
    /// <param name="toScene">切り替えた後のシーン名</param>
    const TransitionPreset *Resolve(const std::string &explicitName, const std::string &fromScene,
                                    const std::string &toScene) const;

    /// <summary>重ならない名前を作る（"名前 (2)" のように）</summary>
    std::string MakeUniqueName(const std::string &base) const;

    std::vector<TransitionPreset> presets;
    std::vector<TransitionRule> rules;
    std::string defaultPreset;
};

// JSON との変換
void to_json(nlohmann::json &json, const TransitionLayer &layer);
void from_json(const nlohmann::json &json, TransitionLayer &layer);
void to_json(nlohmann::json &json, const TransitionSceneFx &fx);
void from_json(const nlohmann::json &json, TransitionSceneFx &fx);
void to_json(nlohmann::json &json, const TransitionPhase &phase);
void from_json(const nlohmann::json &json, TransitionPhase &phase);
void to_json(nlohmann::json &json, const TransitionPreset &preset);
void from_json(const nlohmann::json &json, TransitionPreset &preset);

} // namespace Hagine
