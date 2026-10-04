#include "TransitionPreset.h"
#include <asset/AssetPath.h>
#include <filesystem>
#include <fstream>

namespace Hagine {
namespace {

nlohmann::json ToJson(const Vector2 &v) { return nlohmann::json::array({v.x, v.y}); }
nlohmann::json ToJson(const Vector4 &v) { return nlohmann::json::array({v.x, v.y, v.z, v.w}); }

Vector2 ReadVector2(const nlohmann::json &json, const char *key, const Vector2 &fallback)
{
    if (!json.contains(key) || !json[key].is_array() || json[key].size() < 2)
    {
        return fallback;
    }
    const nlohmann::json &a = json[key];
    return {a[0].get<float>(), a[1].get<float>()};
}

Vector4 ReadVector4(const nlohmann::json &json, const char *key, const Vector4 &fallback)
{
    if (!json.contains(key) || !json[key].is_array() || json[key].size() < 4)
    {
        return fallback;
    }
    const nlohmann::json &a = json[key];
    return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
}

/// <summary>範囲外の番号が保存されていても壊れないよう、列挙を範囲内に収めて読む</summary>
template <typename Enum>
Enum ReadEnum(const nlohmann::json &json, const char *key, Enum fallback, int count)
{
    const int value = json.value(key, static_cast<int>(fallback));
    return (value >= 0 && value < count) ? static_cast<Enum>(value) : fallback;
}

constexpr int kEasingCount = static_cast<int>(EasingType::InOutBounce) + 1;

// ---- ひな形を作るときの道具 ----

TransitionLayer MakeLayer(const char *name, TransitionShape shape, Vector4 color)
{
    TransitionLayer layer;
    layer.name = name;
    layer.shape = shape;
    layer.color = color;
    return layer;
}

TransitionPreset MakePreset(const char *name, const char *description, float cover, float hold, float reveal)
{
    TransitionPreset preset;
    preset.name = name;
    preset.description = description;
    preset.cover.duration = cover;
    preset.hold = hold;
    preset.reveal.duration = reveal;
    return preset;
}

constexpr Vector4 kBlack = {0.0f, 0.0f, 0.0f, 1.0f};
constexpr Vector4 kWhite = {1.0f, 1.0f, 1.0f, 1.0f};

} // namespace

// ============================================================
//  JSON
// ============================================================

void to_json(nlohmann::json &json, const TransitionLayer &layer)
{
    json = nlohmann::json{
        {"enabled", layer.enabled},
        {"name", layer.name},
        {"start", layer.start},
        {"end", layer.end},
        {"easing", static_cast<int>(layer.easing)},
        {"shape", static_cast<int>(layer.shape)},
        {"softness", layer.softness},
        {"invert", layer.invert},
        {"angle", layer.angle},
        {"center", ToJson(layer.center)},
        {"count", layer.count},
        {"order", static_cast<int>(layer.order)},
        {"cellSpread", layer.cellSpread},
        {"amplitude", layer.amplitude},
        {"seed", layer.seed},
        {"ruleImage", layer.ruleImage},
        {"fill", static_cast<int>(layer.fill)},
        {"color", ToJson(layer.color)},
        {"color2", ToJson(layer.color2)},
        {"gradientAngle", layer.gradientAngle},
        {"paletteCount", layer.paletteCount},
        {"image", layer.image},
        {"imageScale", layer.imageScale},
        {"edgeWidth", layer.edgeWidth},
        {"edgeColor", ToJson(layer.edgeColor)},
        {"edgeIntensity", layer.edgeIntensity},
        {"opacity", layer.opacity},
    };
    nlohmann::json palette = nlohmann::json::array();
    for (const Vector4 &c : layer.palette)
    {
        palette.push_back(ToJson(c));
    }
    json["palette"] = palette;
}

void from_json(const nlohmann::json &json, TransitionLayer &layer)
{
    const TransitionLayer d{};
    layer.enabled = json.value("enabled", d.enabled);
    layer.name = json.value("name", d.name);
    layer.start = json.value("start", d.start);
    layer.end = json.value("end", d.end);
    layer.easing = ReadEnum(json, "easing", d.easing, kEasingCount);
    layer.shape = ReadEnum(json, "shape", d.shape, static_cast<int>(TransitionShape::Count));
    layer.softness = json.value("softness", d.softness);
    layer.invert = json.value("invert", d.invert);
    layer.angle = json.value("angle", d.angle);
    layer.center = ReadVector2(json, "center", d.center);
    layer.count = json.value("count", d.count);
    layer.order = ReadEnum(json, "order", d.order, static_cast<int>(TransitionOrder::Count));
    layer.cellSpread = json.value("cellSpread", d.cellSpread);
    layer.amplitude = json.value("amplitude", d.amplitude);
    layer.seed = json.value("seed", d.seed);
    layer.ruleImage = json.value("ruleImage", d.ruleImage);
    layer.fill = ReadEnum(json, "fill", d.fill, static_cast<int>(TransitionFill::Count));
    layer.color = ReadVector4(json, "color", d.color);
    layer.color2 = ReadVector4(json, "color2", d.color2);
    layer.gradientAngle = json.value("gradientAngle", d.gradientAngle);
    layer.paletteCount = json.value("paletteCount", d.paletteCount);
    layer.image = json.value("image", d.image);
    layer.imageScale = json.value("imageScale", d.imageScale);
    layer.edgeWidth = json.value("edgeWidth", d.edgeWidth);
    layer.edgeColor = ReadVector4(json, "edgeColor", d.edgeColor);
    layer.edgeIntensity = json.value("edgeIntensity", d.edgeIntensity);
    layer.opacity = json.value("opacity", d.opacity);
    layer.palette = d.palette;
    if (json.contains("palette") && json["palette"].is_array())
    {
        const nlohmann::json &palette = json["palette"];
        for (size_t i = 0; i < layer.palette.size() && i < palette.size(); ++i)
        {
            nlohmann::json holder = {{"c", palette[i]}};
            layer.palette[i] = ReadVector4(holder, "c", d.palette[i]);
        }
    }
}

void to_json(nlohmann::json &json, const TransitionSceneFx &fx)
{
    json = nlohmann::json{
        {"mosaic", fx.mosaic},   {"blur", fx.blur},     {"swirl", fx.swirl},           {"zoom", fx.zoom},
        {"zoomBlur", fx.zoomBlur}, {"rotate", fx.rotate}, {"chroma", fx.chroma},       {"desaturate", fx.desaturate},
        {"brightness", fx.brightness}, {"shake", fx.shake}, {"wave", fx.wave}, {"easing", static_cast<int>(fx.easing)},
    };
}

void from_json(const nlohmann::json &json, TransitionSceneFx &fx)
{
    const TransitionSceneFx d{};
    fx.mosaic = json.value("mosaic", d.mosaic);
    fx.blur = json.value("blur", d.blur);
    fx.swirl = json.value("swirl", d.swirl);
    fx.zoom = json.value("zoom", d.zoom);
    fx.zoomBlur = json.value("zoomBlur", d.zoomBlur);
    fx.rotate = json.value("rotate", d.rotate);
    fx.chroma = json.value("chroma", d.chroma);
    fx.desaturate = json.value("desaturate", d.desaturate);
    fx.brightness = json.value("brightness", d.brightness);
    fx.shake = json.value("shake", d.shake);
    fx.wave = json.value("wave", d.wave);
    fx.easing = ReadEnum(json, "easing", d.easing, kEasingCount);
}

void to_json(nlohmann::json &json, const TransitionPhase &phase)
{
    json = nlohmann::json{{"duration", phase.duration}, {"layers", phase.layers}, {"sceneFx", phase.sceneFx}};
}

void from_json(const nlohmann::json &json, TransitionPhase &phase)
{
    phase.duration = json.value("duration", 0.5f);
    phase.layers.clear();
    if (json.contains("layers") && json["layers"].is_array())
    {
        for (const nlohmann::json &item : json["layers"])
        {
            if (static_cast<int>(phase.layers.size()) >= TransitionPreset::kMaxLayers)
            {
                break;
            }
            phase.layers.push_back(item.get<TransitionLayer>());
        }
    }
    phase.sceneFx = json.contains("sceneFx") ? json["sceneFx"].get<TransitionSceneFx>() : TransitionSceneFx{};
}

void to_json(nlohmann::json &json, const TransitionPreset &preset)
{
    json = nlohmann::json{
        {"name", preset.name},
        {"description", preset.description},
        {"cover", preset.cover},
        {"hold", preset.hold},
        {"revealMode", static_cast<int>(preset.revealMode)},
        {"reveal", preset.reveal},
    };
}

void from_json(const nlohmann::json &json, TransitionPreset &preset)
{
    preset.name = json.value("name", std::string("遷移"));
    preset.description = json.value("description", std::string());
    if (json.contains("cover"))
    {
        preset.cover = json["cover"].get<TransitionPhase>();
    }
    preset.hold = json.value("hold", 0.1f);
    preset.revealMode = ReadEnum(json, "revealMode", TransitionRevealMode::Reverse, static_cast<int>(TransitionRevealMode::Count));
    if (json.contains("reveal"))
    {
        preset.reveal = json["reveal"].get<TransitionPhase>();
    }
}

// ============================================================
//  一覧
// ============================================================

std::string TransitionLibrary::FilePath()
{
    return AssetPath::Json("Transitions") + "/Transitions.json";
}

void TransitionLibrary::Load()
{
    std::ifstream file(FilePath());
    if (!file)
    {
        // まだ保存されていなければひな形から始める
        presets = BuiltInPresets();
        rules.clear();
        defaultPreset = presets.empty() ? std::string() : presets.front().name;
        return;
    }
    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        presets = BuiltInPresets();
        defaultPreset = presets.front().name;
        return;
    }

    presets.clear();
    if (root.contains("presets") && root["presets"].is_array())
    {
        for (const nlohmann::json &item : root["presets"])
        {
            presets.push_back(item.get<TransitionPreset>());
        }
    }
    if (presets.empty())
    {
        presets = BuiltInPresets();
    }
    rules.clear();
    if (root.contains("rules") && root["rules"].is_array())
    {
        for (const nlohmann::json &item : root["rules"])
        {
            rules.push_back({item.value("from", std::string("*")), item.value("to", std::string("*")),
                             item.value("preset", std::string())});
        }
    }
    defaultPreset = root.value("default", presets.front().name);
    if (!Find(defaultPreset))
    {
        defaultPreset = presets.front().name;
    }
}

bool TransitionLibrary::Save() const
{
    nlohmann::json root = nlohmann::json::object();
    root["default"] = defaultPreset;
    root["presets"] = presets;
    nlohmann::json ruleList = nlohmann::json::array();
    for (const TransitionRule &rule : rules)
    {
        ruleList.push_back({{"from", rule.from}, {"to", rule.to}, {"preset", rule.preset}});
    }
    root["rules"] = ruleList;

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

const TransitionPreset *TransitionLibrary::Find(const std::string &name) const
{
    for (const TransitionPreset &preset : presets)
    {
        if (preset.name == name)
        {
            return &preset;
        }
    }
    return nullptr;
}

TransitionPreset *TransitionLibrary::Find(const std::string &name)
{
    return const_cast<TransitionPreset *>(static_cast<const TransitionLibrary *>(this)->Find(name));
}

const TransitionPreset *TransitionLibrary::Resolve(const std::string &explicitName, const std::string &fromScene,
                                                   const std::string &toScene) const
{
    if (!explicitName.empty())
    {
        if (const TransitionPreset *preset = Find(explicitName))
        {
            return preset;
        }
    }
    for (const TransitionRule &rule : rules)
    {
        const bool fromMatches = rule.from.empty() || rule.from == "*" || rule.from == fromScene;
        const bool toMatches = rule.to.empty() || rule.to == "*" || rule.to == toScene;
        if (fromMatches && toMatches)
        {
            if (const TransitionPreset *preset = Find(rule.preset))
            {
                return preset;
            }
        }
    }
    if (const TransitionPreset *preset = Find(defaultPreset))
    {
        return preset;
    }
    return presets.empty() ? nullptr : &presets.front();
}

std::string TransitionLibrary::MakeUniqueName(const std::string &base) const
{
    if (!Find(base))
    {
        return base;
    }
    for (int i = 2;; ++i)
    {
        const std::string candidate = base + " (" + std::to_string(i) + ")";
        if (!Find(candidate))
        {
            return candidate;
        }
    }
}

// ============================================================
//  ひな形
// ============================================================

std::vector<TransitionPreset> TransitionLibrary::BuiltInPresets()
{
    std::vector<TransitionPreset> list;

    {
        // 以前からの既定（六角形が色とりどりにばらばらに埋まる）
        TransitionPreset p = MakePreset("六角形（4色）", "六角形のマスが4色でばらばらに埋まる。明けるときは最後に埋まったマスから消える", 1.0f, 0.1f, 1.0f);
        TransitionLayer l = MakeLayer("六角形", TransitionShape::Hexagons, kBlack);
        l.count = 11.0f;
        l.fill = TransitionFill::Palette;
        l.order = TransitionOrder::Random;
        l.cellSpread = 0.55f;
        l.softness = 0.02f;
        l.easing = EasingType::InSine;
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("フェード（黒）", "黒く暗転して明ける、いちばん素直な切り替え", 0.45f, 0.1f, 0.45f);
        p.cover.layers.push_back(MakeLayer("黒", TransitionShape::Fade, kBlack));
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("フェード（白）", "白く飛ばして明ける。回想・場面の大きな転換に", 0.35f, 0.1f, 0.6f);
        p.cover.layers.push_back(MakeLayer("白", TransitionShape::Fade, kWhite));
        p.cover.sceneFx.brightness = 0.6f;
        p.cover.sceneFx.blur = 6.0f;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("ワイプ（左から右）", "左から黒が流れ込み、そのまま右へ抜けていく", 0.5f, 0.05f, 0.5f);
        TransitionLayer l = MakeLayer("ワイプ", TransitionShape::Wipe, kBlack);
        l.softness = 0.15f;
        p.cover.layers.push_back(l);
        p.revealMode = TransitionRevealMode::PassThrough;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("斜めの二重ワイプ", "色の帯が先に走り、追いかけるように黒が覆う。縁が光る", 0.6f, 0.05f, 0.6f);
        TransitionLayer accent = MakeLayer("先の帯", TransitionShape::Wipe, {1.0f, 0.55f, 0.15f, 1.0f});
        accent.angle = 30.0f;
        accent.softness = 0.02f;
        accent.end = 0.75f;
        accent.easing = EasingType::OutCubic;
        TransitionLayer black = MakeLayer("黒", TransitionShape::Wipe, kBlack);
        black.angle = 30.0f;
        black.softness = 0.02f;
        black.start = 0.25f;
        black.easing = EasingType::InOutCubic;
        black.edgeWidth = 0.02f;
        black.edgeColor = {1.0f, 0.9f, 0.6f, 1.0f};
        p.cover.layers = {accent, black};
        p.revealMode = TransitionRevealMode::PassThrough;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("アイリス（円が閉じる）", "円が中心へ絞られて閉じ、明けるときは中心から開く", 0.6f, 0.15f, 0.6f);
        TransitionLayer l = MakeLayer("円", TransitionShape::Iris, kBlack);
        l.invert = true;
        l.softness = 0.01f;
        l.easing = EasingType::InCubic;
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("星形で閉じる", "星形に絞られて閉じる。縁が金色に光る", 0.7f, 0.15f, 0.7f);
        TransitionLayer l = MakeLayer("星", TransitionShape::Star, {0.05f, 0.02f, 0.12f, 1.0f});
        l.count = 5.0f;
        l.invert = true;
        l.softness = 0.01f;
        l.edgeWidth = 0.015f;
        l.edgeColor = {1.0f, 0.85f, 0.3f, 1.0f};
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("ハートで閉じる", "ハート形に絞られて閉じる", 0.7f, 0.15f, 0.7f);
        TransitionLayer l = MakeLayer("ハート", TransitionShape::Heart, {0.95f, 0.45f, 0.6f, 1.0f});
        l.invert = true;
        l.softness = 0.01f;
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("時計回り", "12時の位置から時計の針のように覆う", 0.6f, 0.05f, 0.6f);
        TransitionLayer l = MakeLayer("時計", TransitionShape::Clock, kBlack);
        l.count = 1.0f;
        l.softness = 0.01f;
        p.cover.layers.push_back(l);
        p.revealMode = TransitionRevealMode::PassThrough;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("ブラインド", "細い帯が一斉に閉じる", 0.5f, 0.1f, 0.5f);
        TransitionLayer l = MakeLayer("ブラインド", TransitionShape::Blinds, kBlack);
        l.count = 14.0f;
        l.angle = 90.0f;
        l.softness = 0.02f;
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("交互に滑り込む帯", "横の帯が1本おきに左右から順に滑り込む", 0.7f, 0.05f, 0.7f);
        TransitionLayer l = MakeLayer("帯", TransitionShape::Bars, {0.08f, 0.08f, 0.12f, 1.0f});
        l.count = 8.0f;
        l.order = TransitionOrder::Alternate;
        l.cellSpread = 0.45f;
        l.softness = 0.01f;
        l.easing = EasingType::InOutQuad;
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("扉が閉まる", "左右から扉のように閉じ、中央から開く", 0.5f, 0.2f, 0.5f);
        TransitionLayer l = MakeLayer("扉", TransitionShape::Split, kBlack);
        l.softness = 0.005f;
        l.easing = EasingType::OutBounce;
        l.edgeWidth = 0.01f;
        l.edgeColor = {0.8f, 0.8f, 0.9f, 0.6f};
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("タイル", "四角いマスが左上から斜めに埋まっていく", 0.7f, 0.05f, 0.7f);
        TransitionLayer l = MakeLayer("タイル", TransitionShape::Tiles, {0.06f, 0.06f, 0.1f, 1.0f});
        l.count = 9.0f;
        l.order = TransitionOrder::Sweep;
        l.angle = 35.0f;
        l.cellSpread = 0.7f;
        l.softness = 0.02f;
        p.cover.layers.push_back(l);
        p.revealMode = TransitionRevealMode::PassThrough;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("水玉", "丸い点が中心から広がって埋める", 0.7f, 0.05f, 0.7f);
        TransitionLayer l = MakeLayer("水玉", TransitionShape::Dots, {0.1f, 0.6f, 0.9f, 1.0f});
        l.count = 12.0f;
        l.order = TransitionOrder::Center;
        l.cellSpread = 0.6f;
        l.softness = 0.03f;
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("三角形（2色）", "三角形のマスが2色でばらばらに埋まる", 0.8f, 0.05f, 0.8f);
        TransitionLayer l = MakeLayer("三角形", TransitionShape::Triangles, kBlack);
        l.count = 7.0f;
        l.fill = TransitionFill::Palette;
        l.paletteCount = 2;
        l.palette[0] = {0.12f, 0.12f, 0.18f, 1.0f};
        l.palette[1] = {0.95f, 0.75f, 0.2f, 1.0f};
        l.cellSpread = 0.65f;
        l.softness = 0.02f;
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("燃えて溶ける", "ノイズで溶けるように黒くなり、溶けるふちが炎のように光る", 0.9f, 0.1f, 0.9f);
        TransitionLayer l = MakeLayer("溶ける", TransitionShape::Dissolve, kBlack);
        l.count = 5.0f;
        l.softness = 0.01f;
        l.edgeWidth = 0.03f;
        l.edgeColor = {1.0f, 0.45f, 0.1f, 1.0f};
        l.edgeIntensity = 3.0f;
        p.cover.layers.push_back(l);
        p.revealMode = TransitionRevealMode::PassThrough;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("うずまき", "中心から渦を描くように覆う", 0.8f, 0.05f, 0.8f);
        TransitionLayer l = MakeLayer("うずまき", TransitionShape::Spiral, {0.15f, 0.05f, 0.25f, 1.0f});
        l.count = 3.0f;
        l.softness = 0.02f;
        p.cover.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("波打つワイプ", "波打つ境目が上から下へ流れる", 0.6f, 0.05f, 0.6f);
        TransitionLayer l = MakeLayer("波", TransitionShape::Wave, {0.05f, 0.25f, 0.5f, 1.0f});
        l.angle = 90.0f;
        l.count = 3.0f;
        l.amplitude = 0.06f;
        l.softness = 0.03f;
        l.fill = TransitionFill::Gradient;
        l.color = {0.1f, 0.5f, 0.8f, 1.0f};
        l.color2 = {0.02f, 0.08f, 0.25f, 1.0f};
        p.cover.layers.push_back(l);
        p.revealMode = TransitionRevealMode::PassThrough;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("渦に吸い込まれる", "画面がねじれながら寄っていき、黒へ溶ける", 0.8f, 0.1f, 0.6f);
        TransitionLayer l = MakeLayer("黒", TransitionShape::Fade, kBlack);
        l.start = 0.5f;
        l.easing = EasingType::InQuad;
        p.cover.layers.push_back(l);
        p.cover.sceneFx.swirl = 540.0f;
        p.cover.sceneFx.zoom = 1.5f;
        p.cover.sceneFx.easing = EasingType::InCubic;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("モザイク", "画面が粗いモザイクになって暗転し、モザイクのまま明ける", 0.6f, 0.05f, 0.6f);
        TransitionLayer l = MakeLayer("黒", TransitionShape::Fade, kBlack);
        l.start = 0.6f;
        p.cover.layers.push_back(l);
        p.cover.sceneFx.mosaic = 64.0f;
        p.cover.sceneFx.easing = EasingType::OutQuad;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("ズームで突入", "中心へ流れるぼかしで突っ込み、白く飛ぶ", 0.5f, 0.05f, 0.5f);
        TransitionLayer l = MakeLayer("白", TransitionShape::Fade, kWhite);
        l.start = 0.55f;
        p.cover.layers.push_back(l);
        p.cover.sceneFx.zoomBlur = 0.35f;
        p.cover.sceneFx.zoom = 0.6f;
        p.cover.sceneFx.brightness = 0.3f;
        p.cover.sceneFx.easing = EasingType::InQuad;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("ノイズで乱れる", "色ずれ・揺れ・粗いモザイクで乱れて切り替わる（デジタルな場面転換）", 0.4f, 0.05f, 0.4f);
        TransitionLayer l = MakeLayer("黒", TransitionShape::Fade, kBlack);
        l.start = 0.7f;
        p.cover.layers.push_back(l);
        p.cover.sceneFx.chroma = 18.0f;
        p.cover.sceneFx.shake = 14.0f;
        p.cover.sceneFx.mosaic = 10.0f;
        p.cover.sceneFx.wave = 25.0f;
        p.cover.sceneFx.desaturate = 0.5f;
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("クロスフェード", "前の画面がそのまま新しい画面へ溶けていく（暗転しない）", 0.0f, 0.0f, 0.8f);
        TransitionLayer l = MakeLayer("前の画面", TransitionShape::Fade, kBlack);
        l.fill = TransitionFill::PreviousScreen;
        p.revealMode = TransitionRevealMode::Custom;
        p.reveal.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("前の画面をワイプで押し出す", "新しい画面が右から流れ込み、前の画面を押し流す", 0.0f, 0.0f, 0.6f);
        TransitionLayer l = MakeLayer("前の画面", TransitionShape::Wipe, kBlack);
        l.fill = TransitionFill::PreviousScreen;
        l.angle = 180.0f;
        l.softness = 0.01f;
        l.invert = true;
        l.edgeWidth = 0.01f;
        l.edgeColor = {1.0f, 1.0f, 1.0f, 0.8f};
        l.easing = EasingType::InOutCubic;
        p.revealMode = TransitionRevealMode::Custom;
        p.reveal.layers.push_back(l);
        list.push_back(p);
    }
    {
        TransitionPreset p = MakePreset("前の画面が六角形に崩れる", "前の画面が六角形のかけらになって消えていく", 0.0f, 0.0f, 0.9f);
        TransitionLayer l = MakeLayer("前の画面", TransitionShape::Hexagons, kBlack);
        l.fill = TransitionFill::PreviousScreen;
        l.count = 9.0f;
        l.cellSpread = 0.7f;
        l.softness = 0.02f;
        l.edgeWidth = 0.02f;
        l.edgeColor = {0.6f, 0.9f, 1.0f, 0.7f};
        p.revealMode = TransitionRevealMode::Custom;
        p.reveal.layers.push_back(l);
        list.push_back(p);
    }
    return list;
}

} // namespace Hagine
