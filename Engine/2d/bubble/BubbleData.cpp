#include "BubbleData.h"
#include <asset/AssetPath.h>
#include <filesystem>
#include <fstream>

namespace Hagine {
namespace {

nlohmann::json ToJson(const Vector2 &v) { return nlohmann::json::array({v.x, v.y}); }
nlohmann::json ToJson(const Vector3 &v) { return nlohmann::json::array({v.x, v.y, v.z}); }
nlohmann::json ToJson(const Vector4 &v) { return nlohmann::json::array({v.x, v.y, v.z, v.w}); }

/// <summary>
/// 指定キーの float 配列を読み込み（要素が足りなければ fallback）
/// </summary>
template <typename Vector, size_t N>
Vector ReadVector(const nlohmann::json &json, const char *key, const Vector &fallback)
{
    if (!json.contains(key) || !json[key].is_array() || json[key].size() < N)
    {
        return fallback;
    }
    const nlohmann::json &a = json[key];
    Vector result = fallback;
    float *dst = &result.x;
    for (size_t i = 0; i < N; ++i)
    {
        dst[i] = a[i].get<float>();
    }
    return result;
}

/// <summary>
/// 範囲内に収めて列挙を読み込み
/// </summary>
template <typename Enum>
Enum ReadEnum(const nlohmann::json &json, const char *key, Enum fallback, int count)
{
    const int value = json.value(key, static_cast<int>(fallback));
    return (value >= 0 && value < count) ? static_cast<Enum>(value) : fallback;
}

constexpr int kEasingCount = static_cast<int>(EasingType::InOutBounce) + 1;

/// <summary>
/// 一覧の中で重ならない名前を作成
/// </summary>
template <typename Item>
std::string MakeUniqueName(const std::vector<Item> &items, const std::string &base)
{
    auto exists = [&](const std::string &name) {
        for (const Item &item : items)
        {
            if (item.name == name)
            {
                return true;
            }
        }
        return false;
    };
    if (!exists(base))
    {
        return base;
    }
    for (int i = 2;; ++i)
    {
        const std::string candidate = base + std::to_string(i);
        if (!exists(candidate))
        {
            return candidate;
        }
    }
}

} // namespace

// ============================================================
//  JSON
// ============================================================

void to_json(nlohmann::json &json, const BubbleStyle &style)
{
    json = nlohmann::json{
        {"name", style.name},
        {"bodyTexture", style.bodyTexture},
        {"sliceBorder", ToJson(style.sliceBorder)},
        {"edgeMode", static_cast<int>(style.edgeMode)},
        {"bodyColor", ToJson(style.bodyColor)},
        {"tailTexture", style.tailTexture},
        {"tailPivot", ToJson(style.tailPivot)},
        {"tailOverlap", style.tailOverlap},
        {"cornerRadius", style.cornerRadius},
        {"textPadding", ToJson(style.textPadding)},
        {"font", style.font},
        {"fontSize", style.fontSize},
        {"textColor", ToJson(style.textColor)},
        {"lineSpacing", style.lineSpacing},
        {"letterSpacing", style.letterSpacing},
        {"align", static_cast<int>(style.align)},
        {"minWidth", style.minWidth},
        {"maxWidth", style.maxWidth},
        {"wrapMode", static_cast<int>(style.wrapMode)},
        {"wrapWidth", style.wrapWidth},
        {"kinsoku", style.kinsoku},
    };
}

void from_json(const nlohmann::json &json, BubbleStyle &style)
{
    const BubbleStyle d{};
    style.name = json.value("name", d.name);
    style.bodyTexture = json.value("bodyTexture", d.bodyTexture);
    style.sliceBorder = ReadVector<Vector4, 4>(json, "sliceBorder", d.sliceBorder);
    style.edgeMode = ReadEnum(json, "edgeMode", d.edgeMode, static_cast<int>(BubbleEdgeMode::Count));
    style.bodyColor = ReadVector<Vector4, 4>(json, "bodyColor", d.bodyColor);
    style.tailTexture = json.value("tailTexture", d.tailTexture);
    style.tailPivot = ReadVector<Vector2, 2>(json, "tailPivot", d.tailPivot);
    style.tailOverlap = json.value("tailOverlap", d.tailOverlap);
    style.cornerRadius = json.value("cornerRadius", d.cornerRadius);
    style.textPadding = ReadVector<Vector4, 4>(json, "textPadding", d.textPadding);
    style.font = json.value("font", d.font);
    style.fontSize = json.value("fontSize", d.fontSize);
    style.textColor = ReadVector<Vector4, 4>(json, "textColor", d.textColor);
    style.lineSpacing = json.value("lineSpacing", d.lineSpacing);
    style.letterSpacing = json.value("letterSpacing", d.letterSpacing);
    style.align = ReadEnum(json, "align", d.align, static_cast<int>(BubbleTextAlign::Count));
    style.minWidth = json.value("minWidth", d.minWidth);
    style.maxWidth = json.value("maxWidth", d.maxWidth);
    style.wrapMode = ReadEnum(json, "wrapMode", d.wrapMode, static_cast<int>(BubbleWrapMode::Count));
    style.wrapWidth = json.value("wrapWidth", d.wrapWidth);
    style.kinsoku = json.value("kinsoku", d.kinsoku);
}

void to_json(nlohmann::json &json, const BubbleInstance &instance)
{
    json = nlohmann::json{
        {"name", instance.name},
        {"style", instance.style},
        {"text", instance.text},
        {"targetKind", static_cast<int>(instance.targetKind)},
        {"target", instance.target},
        {"worldOffset", ToJson(instance.worldOffset)},
        {"screenPosition", ToJson(instance.screenPosition)},
        {"offset", ToJson(instance.offset)},
        {"pivot", ToJson(instance.pivot)},
        {"showTail", instance.showTail},
        {"tailPosition", instance.tailPosition},
        {"tailFlip", instance.tailFlip},
        {"appear", static_cast<int>(instance.appear)},
        {"appearDuration", instance.appearDuration},
        {"appearEasing", static_cast<int>(instance.appearEasing)},
        {"disappearDuration", instance.disappearDuration},
        {"charInterval", instance.charInterval},
        {"punctuationPause", instance.punctuationPause},
        {"charAppear", static_cast<int>(instance.charAppear)},
        {"charDuration", instance.charDuration},
        {"lifeTime", instance.lifeTime},
    };
    json["motion"] = nlohmann::json{
        {"textShake", instance.motion.textShake},
        {"textWave", instance.motion.textWave},
        {"textSway", instance.motion.textSway},
        {"textGlitch", instance.motion.textGlitch},
        {"bodyShake", instance.motion.bodyShake},
        {"bodySway", instance.motion.bodySway},
        {"speed", instance.motion.speed},
    };
}

void from_json(const nlohmann::json &json, BubbleInstance &instance)
{
    const BubbleInstance d{};
    instance.name = json.value("name", d.name);
    instance.style = json.value("style", d.style);
    instance.text = json.value("text", d.text);
    instance.targetKind = ReadEnum(json, "targetKind", d.targetKind, static_cast<int>(BubbleTargetKind::Count));
    instance.target = json.value("target", d.target);
    instance.worldOffset = ReadVector<Vector3, 3>(json, "worldOffset", d.worldOffset);
    instance.screenPosition = ReadVector<Vector2, 2>(json, "screenPosition", d.screenPosition);
    instance.offset = ReadVector<Vector2, 2>(json, "offset", d.offset);
    instance.pivot = ReadVector<Vector2, 2>(json, "pivot", d.pivot);
    instance.showTail = json.value("showTail", d.showTail);
    instance.tailPosition = json.value("tailPosition", d.tailPosition);
    instance.tailFlip = json.value("tailFlip", d.tailFlip);
    instance.appear = ReadEnum(json, "appear", d.appear, static_cast<int>(BubbleAppear::Count));
    instance.appearDuration = json.value("appearDuration", d.appearDuration);
    instance.appearEasing = ReadEnum(json, "appearEasing", d.appearEasing, kEasingCount);
    instance.disappearDuration = json.value("disappearDuration", d.disappearDuration);
    instance.charInterval = json.value("charInterval", d.charInterval);
    instance.punctuationPause = json.value("punctuationPause", d.punctuationPause);
    instance.charAppear = ReadEnum(json, "charAppear", d.charAppear, static_cast<int>(BubbleCharAppear::Count));
    instance.charDuration = json.value("charDuration", d.charDuration);
    instance.lifeTime = json.value("lifeTime", d.lifeTime);
    if (json.contains("motion") && json["motion"].is_object())
    {
        const nlohmann::json &m = json["motion"];
        instance.motion.textShake = m.value("textShake", d.motion.textShake);
        instance.motion.textWave = m.value("textWave", d.motion.textWave);
        instance.motion.textSway = m.value("textSway", d.motion.textSway);
        instance.motion.textGlitch = m.value("textGlitch", d.motion.textGlitch);
        instance.motion.bodyShake = m.value("bodyShake", d.motion.bodyShake);
        instance.motion.bodySway = m.value("bodySway", d.motion.bodySway);
        instance.motion.speed = m.value("speed", d.motion.speed);
    }
}

// ============================================================
//  一覧
// ============================================================

std::string BubbleLibrary::FilePath()
{
    return AssetPath::Json("Bubbles") + "/Bubbles.json";
}

void BubbleLibrary::Load()
{
    styles.clear();
    instances.clear();

    std::ifstream file(FilePath());
    if (file)
    {
        const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
        if (!root.is_discarded() && root.is_object())
        {
            if (root.contains("styles") && root["styles"].is_array())
            {
                for (const nlohmann::json &item : root["styles"])
                {
                    styles.push_back(item.get<BubbleStyle>());
                }
            }
            if (root.contains("instances") && root["instances"].is_array())
            {
                for (const nlohmann::json &item : root["instances"])
                {
                    instances.push_back(item.get<BubbleInstance>());
                }
            }
        }
    }

    // ひな形
    if (styles.empty())
    {
        styles = BuiltInStyles();
    }
    if (instances.empty())
    {
        instances = BuiltInInstances();
    }
}

std::vector<BubbleStyle> BubbleLibrary::BuiltInStyles()
{
    std::vector<BubbleStyle> list;

    // 角の丸い吹き出し
    {
        BubbleStyle s;
        s.name = "Normal";
        list.push_back(s);
    }
    // ギザギザの叫び
    {
        BubbleStyle s;
        s.name = "Shout";
        s.bodyTexture = "debug/bubble_shout_body.png";
        s.sliceBorder = {32.0f, 32.0f, 32.0f, 32.0f};
        s.edgeMode = BubbleEdgeMode::Tile;
        s.tailTexture = "debug/bubble_shout_tail.png";
        s.tailPivot = {22.0f, 0.0f};
        s.tailOverlap = 20.0f;
        s.cornerRadius = 26.0f;
        s.textPadding = {36.0f, 32.0f, 36.0f, 32.0f};
        s.fontSize = 36.0f;
        s.align = BubbleTextAlign::Center;
        s.minWidth = 180.0f;
        list.push_back(s);
    }
    // 雲形と丸の点々（心の声）
    {
        BubbleStyle s;
        s.name = "Thought";
        s.bodyTexture = "debug/bubble_thought_body.png";
        s.sliceBorder = {32.0f, 32.0f, 32.0f, 32.0f};
        s.edgeMode = BubbleEdgeMode::Tile;
        s.tailTexture = "debug/bubble_thought_tail.png";
        s.tailPivot = {12.0f, 0.0f};
        s.tailOverlap = 0.0f;
        s.cornerRadius = 28.0f;
        s.textPadding = {32.0f, 28.0f, 32.0f, 28.0f};
        s.textColor = {0.25f, 0.25f, 0.3f, 1.0f};
        list.push_back(s);
    }
    // 点線のささやき
    {
        BubbleStyle s;
        s.name = "Whisper";
        s.bodyTexture = "debug/bubble_whisper_body.png";
        s.sliceBorder = {24.0f, 24.0f, 24.0f, 24.0f};
        s.edgeMode = BubbleEdgeMode::Tile;
        s.tailTexture = "debug/bubble_whisper_tail.png";
        s.tailPivot = {20.0f, 0.0f};
        s.tailOverlap = 6.0f;
        s.cornerRadius = 18.0f;
        s.fontSize = 26.0f;
        s.textColor = {0.4f, 0.4f, 0.45f, 1.0f};
        list.push_back(s);
    }
    // 四角いナレーション枠
    {
        BubbleStyle s;
        s.name = "Narration";
        s.bodyTexture = "debug/bubble_box_body.png";
        s.sliceBorder = {16.0f, 16.0f, 16.0f, 16.0f};
        s.tailTexture.clear();
        s.cornerRadius = 0.0f;
        s.textPadding = {24.0f, 18.0f, 24.0f, 18.0f};
        s.font = "PixelMplus12-Regular.ttf";
        s.fontSize = 24.0f;
        s.textColor = {0.95f, 0.95f, 0.95f, 1.0f};
        s.lineSpacing = 1.4f;
        s.minWidth = 240.0f;
        s.maxWidth = 760.0f;
        list.push_back(s);
    }
    // 波打つ輪郭
    {
        BubbleStyle s;
        s.name = "Wobbly";
        s.bodyTexture = "debug/bubble_wavy_body.png";
        s.sliceBorder = {32.0f, 32.0f, 32.0f, 32.0f};
        s.edgeMode = BubbleEdgeMode::Tile;
        s.tailTexture = "debug/bubble_wavy_tail.png";
        s.tailPivot = {20.0f, 0.0f};
        s.tailOverlap = 14.0f;
        s.cornerRadius = 24.0f;
        s.textPadding = {30.0f, 26.0f, 30.0f, 26.0f};
        list.push_back(s);
    }
    return list;
}

std::vector<BubbleInstance> BubbleLibrary::BuiltInInstances()
{
    std::vector<BubbleInstance> list;

    // 普通の会話
    {
        BubbleInstance b;
        b.name = "Talk";
        b.style = "Normal";
        b.text = "吹き出しのテスト。\n文字送りで1文字ずつ出ます。";
        list.push_back(b);
    }
    // 叫び（叩きつけて震える）
    {
        BubbleInstance b;
        b.name = "Shout";
        b.style = "Shout";
        b.text = "うおおお！！\nなんだこれ！！";
        b.appear = BubbleAppear::Stamp;
        b.appearDuration = 0.18f;
        b.appearEasing = EasingType::OutQuad;
        b.charInterval = 0.0f;
        b.charAppear = BubbleCharAppear::Stamp;
        b.charDuration = 0.18f;
        b.motion.textShake = 1.5f;
        b.motion.bodyShake = 2.0f;
        list.push_back(b);
    }
    // 心の声（ふわっと出る）
    {
        BubbleInstance b;
        b.name = "Thought";
        b.style = "Thought";
        b.text = "（……あれ、ここどこだっけ）";
        b.appear = BubbleAppear::Fade;
        b.appearDuration = 0.4f;
        b.appearEasing = EasingType::OutSine;
        b.charInterval = 0.07f;
        b.charAppear = BubbleCharAppear::Fade;
        b.charDuration = 0.3f;
        b.motion.textWave = 1.5f;
        b.motion.speed = 0.5f;
        list.push_back(b);
    }
    // ささやき（ゆっくり薄く）
    {
        BubbleInstance b;
        b.name = "Whisper";
        b.style = "Whisper";
        b.text = "ねえ、聞こえる……？";
        b.appear = BubbleAppear::Fade;
        b.appearDuration = 0.3f;
        b.appearEasing = EasingType::OutSine;
        b.charInterval = 0.09f;
        b.charAppear = BubbleCharAppear::Fade;
        b.charDuration = 0.35f;
        list.push_back(b);
    }
    // ナレーション（画面上部に横から）
    {
        BubbleInstance b;
        b.name = "Narration";
        b.style = "Narration";
        b.text = "――その夜、記憶はここで途切れている。";
        b.screenPosition = {640.0f, 40.0f};
        b.offset = {0.0f, 0.0f};
        b.pivot = {0.5f, 0.0f};
        b.showTail = false;
        b.appear = BubbleAppear::SlideLeft;
        b.appearDuration = 0.3f;
        b.appearEasing = EasingType::OutCubic;
        b.charInterval = 0.04f;
        b.charAppear = BubbleCharAppear::Instant;
        list.push_back(b);
    }
    // ふらふら揺れる
    {
        BubbleInstance b;
        b.name = "Wobbly";
        b.style = "Wobbly";
        b.text = "ふにゃ～、まだまだいけるってぇ～";
        b.appear = BubbleAppear::Spin;
        b.appearDuration = 0.4f;
        b.appearEasing = EasingType::OutBack;
        b.charAppear = BubbleCharAppear::Rise;
        b.charDuration = 0.25f;
        b.motion.textWave = 3.0f;
        b.motion.textSway = 6.0f;
        b.motion.bodySway = 2.0f;
        b.motion.speed = 0.8f;
        list.push_back(b);
    }
    // 文字化け
    {
        BubbleInstance b;
        b.name = "Garbled";
        b.style = "Normal";
        b.text = "記憶が……とぎれ……とぎれに……";
        b.charInterval = 0.06f;
        b.charAppear = BubbleCharAppear::Garble;
        b.charDuration = 0.35f;
        b.motion.textGlitch = 0.25f;
        b.motion.textShake = 0.5f;
        list.push_back(b);
    }
    return list;
}

bool BubbleLibrary::Save() const
{
    nlohmann::json root = nlohmann::json::object();
    root["styles"] = styles;
    root["instances"] = instances;

    const std::filesystem::path path = FilePath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path);
    if (!file)
    {
        return false;
    }
    file << root.dump(2);
    return true;
}

const BubbleStyle *BubbleLibrary::FindStyle(const std::string &name) const
{
    for (const BubbleStyle &style : styles)
    {
        if (style.name == name)
        {
            return &style;
        }
    }
    return nullptr;
}

BubbleStyle *BubbleLibrary::FindStyle(const std::string &name)
{
    return const_cast<BubbleStyle *>(static_cast<const BubbleLibrary *>(this)->FindStyle(name));
}

const BubbleInstance *BubbleLibrary::FindInstance(const std::string &name) const
{
    for (const BubbleInstance &instance : instances)
    {
        if (instance.name == name)
        {
            return &instance;
        }
    }
    return nullptr;
}

BubbleInstance *BubbleLibrary::FindInstance(const std::string &name)
{
    return const_cast<BubbleInstance *>(static_cast<const BubbleLibrary *>(this)->FindInstance(name));
}

std::string BubbleLibrary::MakeUniqueStyleName(const std::string &base) const
{
    return MakeUniqueName(styles, base);
}

std::string BubbleLibrary::MakeUniqueInstanceName(const std::string &base) const
{
    return MakeUniqueName(instances, base);
}

} // namespace Hagine
