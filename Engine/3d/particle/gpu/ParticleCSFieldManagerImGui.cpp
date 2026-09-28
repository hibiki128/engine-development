#define NOMINMAX
#include "ParticleCSFieldManager.h"
// パーティクルフィールド管理窓（エディタ部分）。
//   ・左: 一覧（検索・有効/ソロ・ドラッグで並べ替え・右クリックで複製/削除）
//   ・右: 選択中の1本の詳細
//       形と範囲（球/箱/円柱・位置・回転・大きさ・端の弱まり＋グラフ）
//       効果（必要なものだけ「＋ 効果を追加」で足す。各効果はカードに説明付き）
//       効くエミッター（レイヤーと、実際に受けているエミッターの一覧）
// フィールドはギズモにも登録してあるので、シーンのアイコンやギズモからも選べて、選ぶと一覧が追従する。
#ifdef USE_IMGUI
#include "ParticleCSEditor.h"
#include "ParticleCSEmitter.h"
#include "../utility/debug/imgui/ImGuizmoManager.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <implot.h>
#include <numbers>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#include "ParticleCSFieldLayerUI.h"
#include <edit/undo/ImGuiUndoTracker.h>

namespace {
// フィールド窓の編集ジェスチャを Undo 履歴へ積むトラッカー（ヘッダーのメンバーにすると Undo のヘッダーが広がるのでここに置く）
Hagine::ImGuiUndoTracker g_fieldUndoTracker;
} // namespace

namespace Hagine {
namespace {
// 右クリックの「既定値に戻す」に使う値
constexpr float kZero3[3] = {0.0f, 0.0f, 0.0f};
constexpr float kDefaultRadius = 5.0f;
constexpr float kDefaultBox[3] = {10.0f, 10.0f, 10.0f};
constexpr float kDefaultHeight = 10.0f;
constexpr float kDefaultResponse = 2.0f;

// 一覧の並べ替えに使うドラッグ＆ドロップの種類名
constexpr const char *kFieldRowPayload = "PCS_FIELD_ROW";

ImVec4 ToImVec4(const Vector4 &v) { return ImVec4(v.x, v.y, v.z, v.w); }

std::string ToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// <summary>ラジアンの Vector3 を「度」で編集する DragFloat3（右クリックメニュー付き）</summary>
bool DragDegrees3(const char *label, Vector3 &radians)
{
    constexpr float kToDeg = 180.0f / std::numbers::pi_v<float>;
    float degrees[3] = {radians.x * kToDeg, radians.y * kToDeg, radians.z * kToDeg};
    bool changed = ImGui::DragFloat3(label, degrees, 0.5f, -360.0f, 360.0f, "%.1f°");
    changed |= FloatNContextMenu("##rotMenu", degrees, 3, kZero3);
    if (changed)
    {
        radians = {degrees[0] / kToDeg, degrees[1] / kToDeg, degrees[2] / kToDeg};
    }
    return changed;
}

/// <summary>向き（風向き）をワンクリックで ±X/Y/Z にするボタン列</summary>
void AxisButtons(Vector3 &direction)
{
    struct AxisButton
    {
        const char *label;
        Vector3 direction;
    };
    static const AxisButton kAxisButtons[] = {
        {"+X", {1.0f, 0.0f, 0.0f}}, {"-X", {-1.0f, 0.0f, 0.0f}}, {"+Y", {0.0f, 1.0f, 0.0f}},
        {"-Y", {0.0f, -1.0f, 0.0f}}, {"+Z", {0.0f, 0.0f, 1.0f}}, {"-Z", {0.0f, 0.0f, -1.0f}},
    };
    for (int a = 0; a < 6; ++a)
    {
        if (a > 0)
            ImGui::SameLine();
        if (ImGui::SmallButton(kAxisButtons[a].label))
        {
            direction = kAxisButtons[a].direction;
        }
    }
}

// ---- 効果の一覧（追加メニュー・カードの見出しで共用）----
enum class EffectKind
{
    Wind,
    Attract,
    Vortex,
    Drag,
    Tint,
    Size,
    Life,
    Trail,
    Once,
    Spawn,
    Count,
};

struct EffectInfo
{
    const char *icon;
    const char *name;
    const char *description;
    ImVec4 color;
};

const EffectInfo &GetEffectInfo(EffectKind kind)
{
    static const EffectInfo kInfos[static_cast<int>(EffectKind::Count)] = {
        {ICON_FA_WIND, "風", "範囲内の粒子を、風の速さへだんだん近づける（流される動き）", {0.3f, 0.7f, 1.0f, 1.0f}},
        {ICON_FA_MAGNET, "引き寄せ / 押し出し", "中心へ引く（マイナスで押し出す）。中心に着いたら消すこともできる", {0.8f, 0.3f, 1.0f, 1.0f}},
        {ICON_FA_SYNC_ALT, "渦", "形の Y 軸まわりに回す。回転（ギズモ）で軸を傾けられる", {0.2f, 1.0f, 0.6f, 1.0f}},
        {ICON_FA_TACHOMETER_ALT, "抵抗", "速度を落とす（水の中・減速帯）", {0.55f, 0.65f, 0.80f, 1.0f}},
        {ICON_FA_TINT, "色", "範囲内で色を掛ける。端に向かって元の色に戻る", {1.0f, 0.85f, 0.35f, 1.0f}},
        {ICON_FA_EXPAND_ARROWS_ALT, "大きさ", "範囲内で大きさを変える。端に向かって元の大きさに戻る", {0.95f, 0.65f, 0.85f, 1.0f}},
        {ICON_FA_HOURGLASS_HALF, "寿命", "範囲内で早く年を取らせる（早く消える）／入ったらすぐ消す", {0.95f, 0.35f, 0.35f, 1.0f}},
        {ICON_FA_METEOR, "トレイル", "範囲内にいる間だけ軌跡を出す", {0.60f, 0.85f, 0.85f, 1.0f}},
        {ICON_FA_BOLT, "入った瞬間", "範囲に入ったとき1回だけ、寿命・速度・色などを書き換える", {0.60f, 0.85f, 0.85f, 1.0f}},
        {ICON_FA_SEEDLING, "この範囲から発生", "「フィールド接触部分にのみ発生」にしたエミッターが、この範囲に入った表面からだけ粒子を出す",
         {0.55f, 0.90f, 0.45f, 1.0f}},
    };
    return kInfos[static_cast<int>(kind)];
}

bool &EffectEnabled(ParticleField &field, EffectKind kind)
{
    switch (kind)
    {
    case EffectKind::Wind:
        return field.wind.enabled;
    case EffectKind::Attract:
        return field.attract.enabled;
    case EffectKind::Vortex:
        return field.vortex.enabled;
    case EffectKind::Drag:
        return field.drag.enabled;
    case EffectKind::Tint:
        return field.tint.enabled;
    case EffectKind::Size:
        return field.size.enabled;
    case EffectKind::Life:
        return field.life.enabled;
    case EffectKind::Trail:
        return field.trail.enabled;
    case EffectKind::Once:
        return field.once.enabled;
    default:
        return field.spawn.enabled;
    }
}

/// <summary>有効な効果の名前を「、」でつなぐ（一覧のツールチップ・詳細の要約用）</summary>
std::string DescribeEffects(ParticleField &field)
{
    std::string text;
    for (int k = 0; k < static_cast<int>(EffectKind::Count); ++k)
    {
        const EffectKind kind = static_cast<EffectKind>(k);
        if (EffectEnabled(field, kind))
        {
            text += text.empty() ? GetEffectInfo(kind).name : std::string("、") + GetEffectInfo(kind).name;
        }
    }
    return text.empty() ? "なし" : text;
}

const char *ShapeName(ParticleFieldShape shape)
{
    switch (shape)
    {
    case ParticleFieldShape::Box:
        return "箱";
    case ParticleFieldShape::Cylinder:
        return "円柱";
    default:
        return "球";
    }
}

// ---- プリセット ----
struct FieldPreset
{
    const char *name;
    const char *hint;
    void (*build)(ParticleField &field);
};

const FieldPreset kFieldPresets[] = {
    {"そよ風", "広い範囲で、ゆっくり横へ流す",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Sphere;
         f.radius = 15.0f;
         f.falloff = ParticleFieldFalloff::Smooth;
         f.falloffStart = 0.3f;
         f.wind = {true, {1.0f, 0.0f, 0.0f}, 3.0f, 1.0f};
     }},
    {"突風", "箱の中を一気に吹き抜ける",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Box;
         f.boxSize = {20.0f, 10.0f, 20.0f};
         f.falloff = ParticleFieldFalloff::Linear;
         f.falloffStart = 0.5f;
         f.wind = {true, {1.0f, 0.0f, 0.0f}, 15.0f, 4.0f};
     }},
    {"上昇気流", "円柱の中を上へ持ち上げる",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Cylinder;
         f.radius = 4.0f;
         f.height = 12.0f;
         f.falloff = ParticleFieldFalloff::Smooth;
         f.falloffStart = 0.4f;
         f.wind = {true, {0.0f, 1.0f, 0.0f}, 6.0f, 2.0f};
     }},
    {"竜巻", "回しながら中心へ寄せて、上へ巻き上げる",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Cylinder;
         f.radius = 5.0f;
         f.height = 14.0f;
         f.falloff = ParticleFieldFalloff::Smooth;
         f.falloffStart = 0.3f;
         f.vortex = {true, 12.0f, 3.0f};
         f.attract = {true, 6.0f, 0.0f};
         f.wind = {true, {0.0f, 1.0f, 0.0f}, 4.0f, 0.8f};
     }},
    {"渦潮", "平たく広く、ゆっくり回して寄せる",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Cylinder;
         f.radius = 12.0f;
         f.height = 4.0f;
         f.falloff = ParticleFieldFalloff::Smooth;
         f.falloffStart = 0.2f;
         f.vortex = {true, 6.0f, 1.5f};
         f.attract = {true, 3.0f, 0.0f};
     }},
    {"吸い込んで消す", "中心へ吸い寄せ、着いたら消す",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Sphere;
         f.radius = 8.0f;
         f.falloff = ParticleFieldFalloff::Smooth;
         f.falloffStart = 0.2f;
         f.attract = {true, 25.0f, 0.6f};
     }},
    {"爆風", "中心から一気に押し出す",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Sphere;
         f.radius = 6.0f;
         f.falloff = ParticleFieldFalloff::Linear;
         f.falloffStart = 0.0f;
         f.attract = {true, -80.0f, 0.0f};
     }},
    {"減速帯", "箱の中で速度を落とす",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Box;
         f.boxSize = {10.0f, 6.0f, 10.0f};
         f.falloff = ParticleFieldFalloff::Constant;
         f.drag = {true, 3.0f};
     }},
    {"消滅ゾーン", "入った粒子をすぐ消す（壁の向こうに抜けさせない等）",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Box;
         f.boxSize = {6.0f, 6.0f, 6.0f};
         f.falloff = ParticleFieldFalloff::Constant;
         f.life.enabled = true;
         f.life.killOnEnter = true;
     }},
    {"色変えゾーン", "通った粒子の色を変える",
     [](ParticleField &f) {
         f.shape = ParticleFieldShape::Sphere;
         f.radius = 6.0f;
         f.falloff = ParticleFieldFalloff::Smooth;
         f.falloffStart = 0.5f;
         f.tint = {true, {1.0f, 0.4f, 0.1f, 1.0f}};
     }},
};

/// <summary>プリセットの形と効果を当てる（位置・名前・レイヤーは保つ）</summary>
void ApplyPreset(ParticleField &field, const FieldPreset &preset)
{
    ParticleField fresh;
    fresh.name = field.name;
    fresh.enabled = field.enabled;
    fresh.position = field.position;
    fresh.layers = field.layers;
    preset.build(fresh);
    field = fresh;
}
} // namespace

// =============================================
// Undo の状態（JSON）
// =============================================
namespace {
nlohmann::json FieldToJson(const ParticleField &f)
{
    nlohmann::json j;
    j["name"] = f.name;
    j["enabled"] = f.enabled;
    j["shape"] = static_cast<int>(f.shape);
    j["position"] = f.position;
    j["rotation"] = f.rotation;
    j["radius"] = f.radius;
    j["boxSize"] = f.boxSize;
    j["height"] = f.height;
    j["falloff"] = static_cast<int>(f.falloff);
    j["falloffStart"] = f.falloffStart;
    j["layers"] = f.layers;
    j["wind"] = {{"on", f.wind.enabled}, {"dir", f.wind.direction}, {"speed", f.wind.speed}, {"resp", f.wind.response}};
    j["attract"] = {{"on", f.attract.enabled}, {"str", f.attract.strength}, {"absorb", f.attract.absorbRadius}};
    j["vortex"] = {{"on", f.vortex.enabled}, {"speed", f.vortex.speed}, {"resp", f.vortex.response}};
    j["drag"] = {{"on", f.drag.enabled}, {"per", f.drag.perSecond}};
    j["tint"] = {{"on", f.tint.enabled}, {"color", f.tint.color}};
    j["size"] = {{"on", f.size.enabled}, {"scale", f.size.scale}};
    j["life"] = {{"on", f.life.enabled}, {"speed", f.life.speed}, {"kill", f.life.killOnEnter}};
    j["trail"] = {{"on", f.trail.enabled}, {"dist", f.trail.spawnDistance}};
    const ParticleFieldSettingsOverride &ov = f.once.settings;
    j["once"] = {{"on", f.once.enabled},          {"mask", ov.overrideMask},         {"lifeMin", ov.lifeTimeMin},
                 {"lifeMax", ov.lifeTimeMax},     {"scaleMin", ov.scaleMin},         {"scaleMax", ov.scaleMax},
                 {"velMin", ov.velocityMin},      {"velMax", ov.velocityMax},        {"velMul", ov.velocityMultiplier},
                 {"impulse", ov.accelImpulse},    {"color", ov.color},               {"trail", ov.trailSpawnDistance},
                 {"gather", ov.gatherTarget}};
    j["spawn"] = {{"on", f.spawn.enabled}, {"count", f.spawn.count}, {"interval", f.spawn.interval},
                  {"lifeMin", f.spawn.lifeMin}, {"lifeMax", f.spawn.lifeMax}};
    return j;
}

void FieldFromJson(const nlohmann::json &j, ParticleField &f)
{
    f.name = j.value("name", f.name);
    f.enabled = j.value("enabled", f.enabled);
    f.shape = static_cast<ParticleFieldShape>(j.value("shape", 0));
    f.position = j.value("position", f.position);
    f.rotation = j.value("rotation", f.rotation);
    f.radius = j.value("radius", f.radius);
    f.boxSize = j.value("boxSize", f.boxSize);
    f.height = j.value("height", f.height);
    f.falloff = static_cast<ParticleFieldFalloff>(j.value("falloff", 2));
    f.falloffStart = j.value("falloffStart", f.falloffStart);
    f.layers = j.value("layers", f.layers);
    const nlohmann::json &w = j["wind"];
    f.wind = {w.value("on", false), w.value("dir", f.wind.direction), w.value("speed", f.wind.speed), w.value("resp", f.wind.response)};
    const nlohmann::json &a = j["attract"];
    f.attract = {a.value("on", false), a.value("str", f.attract.strength), a.value("absorb", f.attract.absorbRadius)};
    const nlohmann::json &v = j["vortex"];
    f.vortex = {v.value("on", false), v.value("speed", f.vortex.speed), v.value("resp", f.vortex.response)};
    const nlohmann::json &d = j["drag"];
    f.drag = {d.value("on", false), d.value("per", f.drag.perSecond)};
    const nlohmann::json &t = j["tint"];
    f.tint = {t.value("on", false), t.value("color", f.tint.color)};
    const nlohmann::json &sz = j["size"];
    f.size = {sz.value("on", false), sz.value("scale", f.size.scale)};
    const nlohmann::json &l = j["life"];
    f.life = {l.value("on", false), l.value("speed", f.life.speed), l.value("kill", false)};
    const nlohmann::json &tr = j["trail"];
    f.trail = {tr.value("on", false), tr.value("dist", f.trail.spawnDistance)};
    const nlohmann::json &o = j["once"];
    f.once.enabled = o.value("on", false);
    ParticleFieldSettingsOverride &ov = f.once.settings;
    ov.overrideMask = o.value("mask", ov.overrideMask);
    ov.lifeTimeMin = o.value("lifeMin", ov.lifeTimeMin);
    ov.lifeTimeMax = o.value("lifeMax", ov.lifeTimeMax);
    ov.scaleMin = o.value("scaleMin", ov.scaleMin);
    ov.scaleMax = o.value("scaleMax", ov.scaleMax);
    ov.velocityMin = o.value("velMin", ov.velocityMin);
    ov.velocityMax = o.value("velMax", ov.velocityMax);
    ov.velocityMultiplier = o.value("velMul", ov.velocityMultiplier);
    ov.accelImpulse = o.value("impulse", ov.accelImpulse);
    ov.color = o.value("color", ov.color);
    ov.trailSpawnDistance = o.value("trail", ov.trailSpawnDistance);
    ov.gatherTarget = o.value("gather", ov.gatherTarget);
    const nlohmann::json &sp = j["spawn"];
    f.spawn.enabled = sp.value("on", false);
    f.spawn.count = sp.value("count", f.spawn.count);
    f.spawn.interval = sp.value("interval", f.spawn.interval);
    f.spawn.lifeMin = sp.value("lifeMin", f.spawn.lifeMin);
    f.spawn.lifeMax = sp.value("lifeMax", f.spawn.lifeMax);
}
} // namespace

nlohmann::json ParticleCSFieldManager::CaptureUndoState() const
{
    nlohmann::json list = nlohmann::json::array();
    for (const ParticleField &field : fields_)
    {
        list.push_back(FieldToJson(field));
    }
    return nlohmann::json{{"fields", list}};
}

void ParticleCSFieldManager::RestoreUndoState(const nlohmann::json &state)
{
    if (!state.contains("fields") || !state["fields"].is_array())
    {
        return;
    }
    const nlohmann::json &list = state["fields"];
    // 並び（容量は確保済み）をそのまま使い回し、ゲーム側が持つポインタがずれないようにする
    fields_.resize((std::min)(list.size(), static_cast<size_t>(kMaxFields)));
    for (size_t i = 0; i < fields_.size(); ++i)
    {
        ParticleField restored;
        FieldFromJson(list[i], restored);
        restored.spawn.timer = fields_[i].spawn.timer; // 実行時のタイマーは保つ
        fields_[i] = restored;
    }
    const int count = static_cast<int>(fields_.size());
    if (selectedIndex_ >= count)
        selectedIndex_ = count - 1;
    if (soloIndex_ >= count)
        soloIndex_ = -1;
}

// =============================================
// 窓
// =============================================
void ParticleCSFieldManager::DrawImGui()
{
    ImGui::SetNextWindowSize(ImVec2(680, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("パーティクルフィールド管理", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }
    // 窓の中の編集ジェスチャ（数値・ボタン・ギズモのドラッグ）を Undo 履歴へ
    g_fieldUndoTracker.Begin([this] { return CaptureUndoState(); });

    SyncSelectionFromGizmo();
    if (selectedIndex_ >= static_cast<int>(fields_.size()))
    {
        selectedIndex_ = static_cast<int>(fields_.size()) - 1;
    }

    // ---- 操作バー ----
    ImGui::BeginDisabled(fields_.size() >= kMaxFields);
    if (PrimaryButton(ICON_FA_PLUS " 追加"))
    {
        ImGui::OpenPopup("##addField");
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("空のフィールド、またはプリセットから追加します（カメラの前に置きます）");
    if (ImGui::BeginPopup("##addField"))
    {
        DrawAddFieldMenuItems();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedIndex_ < 0 || fields_.size() >= kMaxFields);
    if (NeutralButton(ICON_FA_CLONE " 複製"))
    {
        DuplicateField(selectedIndex_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%d / %u", static_cast<int>(fields_.size()), kMaxFields);
    ImGui::SameLine();
    AccentCheckbox("範囲を全部表示", &showGizmos_, DebugTheme::kAccentCyan);
    ImGui::SetItemTooltip("有効なフィールドすべての範囲と向きを線で表示します。\nOFF でも選択中のものは表示します");
    if (soloIndex_ >= 0 && soloIndex_ < static_cast<int>(fields_.size()))
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentYellow);
        ImGui::Text(ICON_FA_HEADPHONES " ソロ: %s", fields_[soloIndex_].name.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("解除##solo"))
        {
            soloIndex_ = -1;
        }
    }
    ImGui::Separator();

    // ---- 左: 一覧 / 右: 詳細 ----
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float paneHeight = (std::max)(200.0f, avail.y);
    const float listWidth = std::clamp(avail.x * 0.32f, 170.0f, 260.0f);
    ImGui::BeginChild("##fieldList", ImVec2(listWidth, paneHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    DrawFieldList(paneHeight);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##fieldDetail", ImVec2(0.0f, paneHeight), ImGuiChildFlags_Borders);
    if (selectedIndex_ >= 0)
    {
        DrawFieldDetail(selectedIndex_);
    }
    else
    {
        DimText(fields_.empty() ? "フィールドがありません。「追加」から作れます" : "左の一覧からフィールドを選んでください");
    }
    ImGui::EndChild();

    // ---- 範囲の線 ----
    if (showGizmos_)
    {
        DrawFieldGizmos();
    }
    else if (selectedIndex_ >= 0)
    {
        DrawFieldGizmo(selectedIndex_);
    }

    g_fieldUndoTracker.End(
        "フィールド編集", [this] { return CaptureUndoState(); },
        [](const nlohmann::json &s) { ParticleCSFieldManager::GetInstance()->RestoreUndoState(s); }, ImGuizmo::IsUsing());
    ImGui::End();
}

// =============================================
// 一覧
// =============================================
void ParticleCSFieldManager::DrawFieldList(float /*height*/)
{
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##fieldSearch", ICON_FA_SEARCH " 検索", searchBuffer_, sizeof(searchBuffer_));
    const std::string needle = ToLower(searchBuffer_);

    // 一覧を回している最中に並びを変えると壊れるので、操作は最後にまとめて行う
    int pendingRemove = -1;
    int pendingDuplicate = -1;
    int moveFrom = -1;
    int moveTo = -1;

    const float rowHeight = ImGui::GetFrameHeight();
    const ImGuiStyle &style = ImGui::GetStyle();
    for (int i = 0; i < static_cast<int>(fields_.size()); ++i)
    {
        ParticleField &field = fields_[i];
        if (!needle.empty() && ToLower(field.name).find(needle) == std::string::npos)
        {
            continue;
        }
        const ImVec4 typeColor = ToImVec4(FieldColor(field));
        const bool isSolo = (soloIndex_ == i);
        const bool dimmed = !field.enabled || (soloIndex_ >= 0 && !isSolo);

        ImGui::PushID(i);

        // 有効 / 無効（目のアイコン）
        {
            ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(field.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled));
            if (ImGui::Button(field.enabled ? ICON_FA_EYE : ICON_FA_EYE_SLASH, ImVec2(rowHeight, rowHeight)))
            {
                field.enabled = !field.enabled;
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("有効 / 無効（ミュート）");
        }
        ImGui::SameLine(0.0f, 2.0f);

        // 名前の行（クリックで選択・ダブルクリックでカメラを寄せる・ドラッグで並べ替え）
        const float rowWidth = (std::max)(40.0f, ImGui::GetContentRegionAvail().x - rowHeight - style.ItemSpacing.x);
        const bool selected = (selectedIndex_ == i);
        if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(rowWidth, rowHeight)))
        {
            SelectField(i);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                ImGuizmoManager::GetInstance()->FocusOnSelection();
            }
        }
        ImGui::SetItemTooltip("%s ・ %s\n効果: %s", ShapeName(field.shape), FieldSummary(field), DescribeEffects(field).c_str());
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(kFieldRowPayload, &i, sizeof(int));
            ImGui::Text("%s %s", FieldIcon(field), field.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kFieldRowPayload))
            {
                moveFrom = *static_cast<const int *>(payload->Data);
                moveTo = i;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("##rowMenu"))
        {
            if (ImGui::MenuItem(ICON_FA_CLONE " 複製", nullptr, false, fields_.size() < kMaxFields))
                pendingDuplicate = i;
            if (ImGui::MenuItem(ICON_FA_ARROW_UP " 上へ", nullptr, false, i > 0))
            {
                moveFrom = i;
                moveTo = i - 1;
            }
            if (ImGui::MenuItem(ICON_FA_ARROW_DOWN " 下へ", nullptr, false, i + 1 < static_cast<int>(fields_.size())))
            {
                moveFrom = i;
                moveTo = i + 1;
            }
            if (ImGui::MenuItem(ICON_FA_HEADPHONES " ソロ", nullptr, isSolo))
                soloIndex_ = isSolo ? -1 : i;
            if (ImGui::MenuItem(ICON_FA_SAVE " 保存"))
                SaveField(field);
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_TRASH " 削除"))
                pendingRemove = i;
            ImGui::EndPopup();
        }

        // 行の中身（代表の効果の色のアイコン＋名前）は描画リストで重ねる
        {
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const float textY = rowMin.y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
            ImVec4 iconColor = typeColor;
            if (dimmed)
                iconColor.w = 0.45f;
            drawList->AddText(ImVec2(rowMin.x + 4.0f, textY), ImGui::ColorConvertFloat4ToU32(iconColor), FieldIcon(field));
            drawList->AddText(ImVec2(rowMin.x + 4.0f + ImGui::GetFontSize() * 1.4f, textY),
                              ImGui::GetColorU32(dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text), field.name.c_str());
        }

        // ソロ
        ImGui::SameLine();
        {
            ScopedButtonColors colors(isSolo ? DebugTheme::kHeaderYellow : DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
            ImGui::PushStyleColor(ImGuiCol_Text, isSolo ? DebugTheme::kAccentYellow : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Button("S", ImVec2(rowHeight, rowHeight)))
            {
                soloIndex_ = isSolo ? -1 : i;
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("ソロ: このフィールドだけを効かせる（もう一度で解除・保存はされません）");
        }

        ImGui::PopID();
    }

    if (fields_.empty())
    {
        DimText("まだありません");
    }

    if (pendingDuplicate >= 0)
    {
        DuplicateField(pendingDuplicate);
    }
    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo)
    {
        MoveField(moveFrom, moveTo);
    }
    if (pendingRemove >= 0)
    {
        RemoveFieldAt(pendingRemove);
    }
}

void ParticleCSFieldManager::DrawAddFieldMenuItems()
{
    auto addField = [this](const std::string &baseName, const FieldPreset *preset) {
        if (fields_.size() >= kMaxFields)
            return;
        ParticleField field;
        field.name = MakeUniqueFieldName(baseName);
        field.enabled = true;
        if (preset)
        {
            ApplyPreset(field, *preset);
        }
        // 原点に出すと探しに行く手間がかかるので、カメラの前に置く
        field.position = ImGuizmoManager::GetInstance()->GetSpawnPosition(12.0f);
        AddField(field);
        SelectField(static_cast<int>(fields_.size()) - 1);
        ImGuiNotification::Post("フィールドを追加しました: " + field.name, {0.4f, 0.8f, 1.0f, 1.0f});
    };

    if (ImGui::MenuItem(ICON_FA_PLUS " 空のフィールド"))
    {
        addField("Field", nullptr);
    }
    ImGui::SetItemTooltip("効果なしの球。「＋ 効果を追加」で必要なものを足していきます");
    ImGui::SeparatorText("プリセット");
    for (const FieldPreset &preset : kFieldPresets)
    {
        ParticleField sample;
        preset.build(sample);
        const std::string label = std::format("{} {}", FieldIcon(sample), preset.name);
        ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(FieldColor(sample)));
        const bool clicked = ImGui::MenuItem(label.c_str());
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s\n形: %s / 効果: %s", preset.hint, ShapeName(sample.shape), DescribeEffects(sample).c_str());
        if (clicked)
        {
            addField(preset.name, &preset);
        }
    }
}

// =============================================
// 詳細
// =============================================
void ParticleCSFieldManager::DrawFieldDetail(int index)
{
    if (index < 0 || index >= static_cast<int>(fields_.size()))
    {
        return;
    }
    ParticleField &f = fields_[index];
    bool requestRemove = false;

    ImGui::PushID("fieldDetail");
    ImGui::PushID(index);

    // ---- 見出し: 代表の効果のアイコン・名前・操作 ----
    ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(FieldColor(f)));
    ImGui::TextUnformatted(FieldIcon(f));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    {
        char nameBuf[128];
        strncpy_s(nameBuf, f.name.c_str(), sizeof(nameBuf) - 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)) && nameBuf[0] != '\0')
        {
            f.name = nameBuf;
        }
        ImGui::SetItemTooltip("名前（保存ファイル名にもなります）");
    }
    AccentCheckbox("有効", &f.enabled, DebugTheme::kAccentGreen);
    ImGui::SameLine();
    bool solo = (soloIndex_ == index);
    if (AccentCheckbox("ソロ", &solo, DebugTheme::kAccentYellow))
    {
        soloIndex_ = solo ? index : -1;
    }
    ImGui::SetItemTooltip("このフィールドだけを効かせます（保存はされません）");
    ImGui::SameLine();
    if (PrimaryButton(ICON_FA_SAVE " 保存"))
    {
        SaveField(f);
    }
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_MAGIC "##preset"))
    {
        ImGui::OpenPopup("##presetPopup");
    }
    ImGui::SetItemTooltip("プリセットを当てる（形と効果をまとめて置き換え。位置・名前・レイヤーはそのまま）");
    if (ImGui::BeginPopup("##presetPopup"))
    {
        for (const FieldPreset &preset : kFieldPresets)
        {
            ParticleField sample;
            preset.build(sample);
            const std::string label = std::format("{} {}", FieldIcon(sample), preset.name);
            ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(FieldColor(sample)));
            const bool clicked = ImGui::MenuItem(label.c_str());
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", preset.hint);
            if (clicked)
            {
                ApplyPreset(f, preset);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_CROSSHAIRS "##focus"))
    {
        SelectField(index);
        ImGuizmoManager::GetInstance()->FocusOnSelection();
    }
    ImGui::SetItemTooltip("シーンのカメラを寄せる");
    ImGui::SameLine();
    if (DangerButton(ICON_FA_TRASH "##remove"))
    {
        requestRemove = true;
    }
    ImGui::SetItemTooltip("削除");

    // 何のフィールドかを1行で
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped("%s ・ 効果: %s ・ レイヤー: %s", ShapeName(f.shape), DescribeEffects(f).c_str(),
                       FieldLayerUI::DescribeLayers(f.layers).c_str());
    ImGui::PopStyleColor();

    ImGui::Spacing();
    DrawShapeSection(f);
    DrawEffectsSection(f, index);
    DrawLayerSection(f);

    ImGui::PopID();
    ImGui::PopID();

    if (requestRemove)
    {
        RemoveFieldAt(index);
    }
}

void ParticleCSFieldManager::DrawShapeSection(ParticleField &f)
{
    if (!ThemedHeader("形と範囲", DebugTheme::kAccentBlue, true))
    {
        return;
    }
    // ---- 形 ----
    struct ShapeButton
    {
        ParticleFieldShape shape;
        const char *label;
        const char *tip;
    };
    static const ShapeButton kShapes[] = {
        {ParticleFieldShape::Sphere, ICON_FA_CIRCLE " 球", "中心からの距離で範囲を決める"},
        {ParticleFieldShape::Box, ICON_FA_CUBE " 箱", "幅・高さ・奥行きの箱。通路や部屋に"},
        {ParticleFieldShape::Cylinder, ICON_FA_DATABASE " 円柱", "竜巻や柱に。軸は回転の Y 軸"},
    };
    for (int s = 0; s < 3; ++s)
    {
        if (s > 0)
            ImGui::SameLine();
        const bool current = (f.shape == kShapes[s].shape);
        if (current ? PrimaryButton(kShapes[s].label) : NeutralButton(kShapes[s].label))
        {
            f.shape = kShapes[s].shape;
        }
        ImGui::SetItemTooltip("%s", kShapes[s].tip);
    }

    ImGui::PushItemWidth(-110.0f);
    ImGui::DragFloat3("位置", &f.position.x, 0.1f, -9999.0f, 9999.0f, "%.2f");
    FloatNContextMenu("##posMenu", &f.position.x, 3, kZero3);
    DragDegrees3("回転", f.rotation);
    ImGui::SetItemTooltip("箱・円柱の向きと、渦の軸（形の Y 軸）を決めます。シーンのギズモで回すこともできます");
    switch (f.shape)
    {
    case ParticleFieldShape::Box:
        ImGui::DragFloat3("大きさ（幅・高さ・奥行き）", &f.boxSize.x, 0.1f, 0.02f, 9999.0f, "%.2f");
        FloatNContextMenu("##boxMenu", &f.boxSize.x, 3, kDefaultBox);
        break;
    case ParticleFieldShape::Cylinder:
        ImGui::DragFloat("半径", &f.radius, 0.1f, 0.01f, 9999.0f, "%.2f");
        FloatNContextMenu("##radMenu", &f.radius, 1, &kDefaultRadius);
        ImGui::DragFloat("高さ", &f.height, 0.1f, 0.02f, 9999.0f, "%.2f");
        FloatNContextMenu("##heightMenu", &f.height, 1, &kDefaultHeight);
        break;
    default:
        ImGui::DragFloat("半径", &f.radius, 0.1f, 0.01f, 9999.0f, "%.2f");
        FloatNContextMenu("##radMenu", &f.radius, 1, &kDefaultRadius);
        break;
    }
    ImGui::PopItemWidth();

    // ---- 端の弱まり ----
    ImGui::Spacing();
    CaptionText("端に向かっての弱まり方");
    struct FalloffButton
    {
        ParticleFieldFalloff falloff;
        const char *label;
        const char *tip;
    };
    static const FalloffButton kFalloffs[] = {
        {ParticleFieldFalloff::Constant, "一定", "範囲内はどこでも 100%。境目ではっきり切れる"},
        {ParticleFieldFalloff::Linear, "直線", "芯の外から端に向かって、まっすぐ弱くなる"},
        {ParticleFieldFalloff::Smooth, "なめらか", "芯の外から端に向かって、なめらかに弱くなる（境目が目立たない）"},
    };
    for (int s = 0; s < 3; ++s)
    {
        if (s > 0)
            ImGui::SameLine();
        const bool current = (f.falloff == kFalloffs[s].falloff);
        if (current ? PrimaryButton(kFalloffs[s].label) : NeutralButton(kFalloffs[s].label))
        {
            f.falloff = kFalloffs[s].falloff;
        }
        ImGui::SetItemTooltip("%s", kFalloffs[s].tip);
    }
    ImGui::BeginDisabled(f.falloff == ParticleFieldFalloff::Constant);
    float corePercent = f.falloffStart * 100.0f;
    ImGui::SetNextItemWidth(-110.0f);
    if (ImGui::SliderFloat("芯の大きさ", &corePercent, 0.0f, 95.0f, "%.0f%%"))
    {
        f.falloffStart = corePercent / 100.0f;
    }
    ImGui::SetItemTooltip("この割合より内側は 100%% で効き、そこから端に向かって弱くなります（シーンでは薄い線）");
    ImGui::EndDisabled();

    // 中心→端での効き具合（シェーダーの EvaluateField と同じ式）
    constexpr int kSamples = 48;
    float xs[kSamples];
    float ys[kSamples];
    const float start = std::clamp(f.falloffStart, 0.0f, 0.95f);
    for (int s = 0; s < kSamples; ++s)
    {
        const float n = static_cast<float>(s) / static_cast<float>(kSamples - 1);
        const float t = std::clamp((n - start) / (std::max)(1.0f - start, 1e-4f), 0.0f, 1.0f);
        float w = 1.0f;
        if (f.falloff == ParticleFieldFalloff::Linear)
            w = 1.0f - t;
        else if (f.falloff == ParticleFieldFalloff::Smooth)
            w = 1.0f - t * t * (3.0f - 2.0f * t);
        xs[s] = n * 100.0f;
        ys[s] = w * 100.0f;
    }
    const ImVec4 color = ToImVec4(FieldColor(f));
    if (ImPlot::BeginPlot("##falloffPlot", ImVec2(-1.0f, 110.0f),
                          ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText | ImPlotFlags_NoLegend))
    {
        ImPlot::SetupAxes("中心 → 端 (%)", "効き (%)", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxesLimits(0.0, 100.0, 0.0, 105.0, ImPlotCond_Always);
        ImPlot::SetNextFillStyle(color, 0.25f);
        ImPlot::PlotShaded("##area", xs, ys, kSamples);
        ImPlot::SetNextLineStyle(color, 2.0f);
        ImPlot::PlotLine("##curve", xs, ys, kSamples);
        ImPlot::EndPlot();
    }
}

void ParticleCSFieldManager::DrawEffectsSection(ParticleField &f, int index)
{
    if (!ThemedHeader("効果", DebugTheme::kAccentOrange, true))
    {
        return;
    }

    // ---- 追加メニュー（まだ足していない効果だけ）----
    if (PrimaryButton(ICON_FA_PLUS " 効果を追加"))
    {
        ImGui::OpenPopup("##addEffect");
    }
    if (ImGui::BeginPopup("##addEffect"))
    {
        bool any = false;
        for (int k = 0; k < static_cast<int>(EffectKind::Count); ++k)
        {
            const EffectKind kind = static_cast<EffectKind>(k);
            bool &enabled = EffectEnabled(f, kind);
            if (enabled)
                continue;
            any = true;
            const EffectInfo &info = GetEffectInfo(kind);
            ImGui::PushStyleColor(ImGuiCol_Text, info.color);
            const bool clicked = ImGui::MenuItem(std::format("{} {}", info.icon, info.name).c_str());
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", info.description);
            if (clicked)
            {
                enabled = true;
            }
        }
        if (!any)
        {
            ImGui::TextDisabled("全部足してあります");
        }
        ImGui::EndPopup();
    }

    // ---- 効果のカード ----
    int shown = 0;
    for (int k = 0; k < static_cast<int>(EffectKind::Count); ++k)
    {
        const EffectKind kind = static_cast<EffectKind>(k);
        bool &enabled = EffectEnabled(f, kind);
        if (!enabled)
            continue;
        ++shown;
        const EffectInfo &info = GetEffectInfo(kind);

        ImGui::PushID(k);
        ImVec4 cardBg = info.color;
        cardBg.w = 0.07f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
        ImGui::BeginChild("##card", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleColor();

        // 見出し: アイコン・名前・説明・外す
        ImGui::PushStyleColor(ImGuiCol_Text, info.color);
        ImGui::TextUnformatted(info.icon);
        ImGui::SameLine();
        ImGui::TextUnformatted(info.name);
        ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
        {
            ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonDangerHover);
            if (ImGui::Button(ICON_FA_TIMES, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
            {
                enabled = false;
            }
            ImGui::SetItemTooltip("この効果を外す");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("%s", info.description);
        ImGui::PopStyleColor();

        ImGui::PushItemWidth(-130.0f);
        switch (kind)
        {
        case EffectKind::Wind:
        {
            ImGui::DragFloat3("風向き", &f.wind.direction.x, 0.01f, -1.0f, 1.0f, "%.2f");
            FloatNContextMenu("##windDirMenu", &f.wind.direction.x, 3, nullptr);
            ImGui::SetItemTooltip("長さは関係ありません（向きだけ使います）");
            AxisButtons(f.wind.direction);
            ImGui::DragFloat("風速", &f.wind.speed, 0.05f, -500.0f, 500.0f, "%.2f 単位/秒");
            ImGui::SetItemTooltip("粒子はこの速さに近づいていきます（止まっている粒子も、この速さまで流される）");
            ImGui::SliderFloat("なじむ速さ", &f.wind.response, 0.05f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            FloatNContextMenu("##windRespMenu", &f.wind.response, 1, &kDefaultResponse);
            ImGui::SetItemTooltip("大きいほど、すぐ風と同じ速さになる（小さいとふんわり流される）");
            ImGui::TextDisabled("中心では約 %.2f 秒で風の速さの 63%% になじみます", 1.0f / (std::max)(f.wind.response, 0.01f));
            break;
        }
        case EffectKind::Attract:
        {
            ImGui::DragFloat("強さ", &f.attract.strength, 0.1f, -1000.0f, 1000.0f, "%.2f 単位/秒²");
            ImGui::SetItemTooltip("中心へ向かう加速度。マイナスで中心から押し出します");
            ImGui::SameLine();
            ImGui::TextDisabled(f.attract.strength >= 0.0f ? "引き寄せる" : "押し出す");
            ImGui::DragFloat("中心で消す距離", &f.attract.absorbRadius, 0.02f, 0.0f, 100.0f, "%.2f");
            ImGui::SetItemTooltip("中心からこの距離まで来た粒子を消します（0 で消さない）。吸い込み演出に");
            break;
        }
        case EffectKind::Vortex:
        {
            ImGui::DragFloat("回る速さ", &f.vortex.speed, 0.05f, -500.0f, 500.0f, "%.2f 単位/秒");
            ImGui::SetItemTooltip("軸まわりの速さ。マイナスで逆回り。\n回る向きは上（形の Y 軸）から見て反時計回りが正");
            ImGui::SliderFloat("なじむ速さ", &f.vortex.response, 0.05f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            FloatNContextMenu("##vortexRespMenu", &f.vortex.response, 1, &kDefaultResponse);
            ImGui::SetItemTooltip("大きいほど、すぐこの速さで回り始める");
            ImGui::TextDisabled("軸は形の回転の Y 軸。外へ飛ばず回り続けます（寄せたいときは「引き寄せ」も足す）");
            break;
        }
        case EffectKind::Drag:
        {
            ImGui::SliderFloat("減速の強さ", &f.drag.perSecond, 0.0f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::TextDisabled("中心では 1 秒で速度が %.0f%% になります", std::exp(-f.drag.perSecond) * 100.0f);
            break;
        }
        case EffectKind::Tint:
        {
            ImGui::ColorEdit4("掛ける色", &f.tint.color.x, ImGuiColorEditFlags_Float);
            ImGui::SetItemTooltip("粒子の色にこの色を掛けます（白 = 変化なし）。アルファを下げると薄くなります");
            break;
        }
        case EffectKind::Size:
        {
            ImGui::SliderFloat("大きさの倍率", &f.size.scale, 0.0f, 5.0f, "%.2f 倍");
            break;
        }
        case EffectKind::Life:
        {
            ImGui::Checkbox("入ったらすぐ消す", &f.life.killOnEnter);
            ImGui::BeginDisabled(f.life.killOnEnter);
            ImGui::SliderFloat("寿命の進み", &f.life.speed, 0.0f, 20.0f, "%.2f 倍");
            ImGui::SetItemTooltip("1 = 普段どおり / 3 = 3倍の速さで年を取る / 0 = 範囲内では年を取らない");
            if (!f.life.killOnEnter && f.life.speed > 0.0f)
            {
                ImGui::TextDisabled("寿命 1 秒の粒子は、中心にいると %.2f 秒で消えます", 1.0f / f.life.speed);
            }
            ImGui::EndDisabled();
            break;
        }
        case EffectKind::Trail:
        {
            ImGui::DragFloat("出す間隔", &f.trail.spawnDistance, 0.005f, 0.0f, 10.0f, "%.3f");
            ImGui::SetItemTooltip("軌跡の粒を出す距離の間隔。0 ならグループ設定の間隔を使います");
            break;
        }
        case EffectKind::Once:
        {
            DrawOverrideImGui(f.once.settings, index);
            if (f.once.settings.overrideMask == 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
                ImGui::TextUnformatted("書き換える項目を1つ以上チェックしてください");
                ImGui::PopStyleColor();
            }
            break;
        }
        case EffectKind::Spawn:
        {
            int count = static_cast<int>(f.spawn.count);
            if (ImGui::DragInt("1回に出す数", &count, 10, 0, 50000))
            {
                f.spawn.count = static_cast<uint32_t>((std::max)(0, count));
            }
            ImGui::SetItemTooltip("受けるエミッターごとの数。範囲に入った表面の点から出るので 500〜3000 で十分密になります");
            ImGui::DragFloat("間隔", &f.spawn.interval, 0.005f, 0.0f, 10.0f, "%.3f 秒");
            ImGui::SetItemTooltip("0 = 毎フレーム出します");
            float life[2] = {f.spawn.lifeMin, f.spawn.lifeMax};
            if (ImGui::DragFloat2("寿命 最小 / 最大", life, 0.01f, 0.0f, 60.0f, "%.2f 秒"))
            {
                f.spawn.lifeMin = (std::min)(life[0], life[1]);
                f.spawn.lifeMax = life[1];
            }
            if (f.spawn.interval > 0.0f)
            {
                const float ratio = std::clamp(f.spawn.timer / f.spawn.interval, 0.0f, 1.0f);
                ImGui::ProgressBar(ratio, ImVec2(180.0f, 0.0f), "");
                ImGui::SameLine();
                ImGui::TextDisabled("次まで %.2f 秒", (std::max)(0.0f, f.spawn.interval - f.spawn.timer));
            }
            // 実際にここから出すエミッター
            int contactEmitters = 0;
            ParticleCSEditor *editor = ParticleCSEditor::GetInstance();
            for (const std::string &emitterName : editor->GetEmitterNames())
            {
                const ParticleCSEmitter *emitter = editor->GetEmitterByName(emitterName);
                if (emitter && emitter->GetReceiveFields() && emitter->GetEmitOnlyOnFieldContact() &&
                    (emitter->GetFieldLayers() & f.layers) != 0)
                {
                    ++contactEmitters;
                }
            }
            if (contactEmitters == 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
                ImGui::TextWrapped("ここから出すエミッターがありません。エミッター側で「フィールド接触部分にのみ発生」を ON にし、レイヤーを重ねてください");
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextDisabled("%d 個のエミッターがここから出します", contactEmitters);
            }
            break;
        }
        default:
            break;
        }
        ImGui::PopItemWidth();

        ImGui::EndChild();
        ImGui::PopID();
    }

    if (shown == 0)
    {
        DimText("効果がありません（何もしない範囲です）。「＋ 効果を追加」から足してください");
    }
}

void ParticleCSFieldManager::DrawLayerSection(ParticleField &f)
{
    if (!ThemedHeader(std::format("効くエミッター（レイヤー: {}）###layers", FieldLayerUI::DescribeLayers(f.layers)).c_str(),
                      DebugTheme::kAccentPurple))
    {
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("効くレイヤー");
    ImGui::SameLine();
    FieldLayerUI::DrawLayerChips("##fieldLayers", f.layers);
    ImGui::TextDisabled("エミッター側の「受けるレイヤー」と1つでも重なれば効きます");

    // 実際にこのフィールドを受けているエミッター（押すとそのエミッターを選ぶ）
    CaptionText("このフィールドを受けるエミッター");
    ParticleCSEditor *editor = ParticleCSEditor::GetInstance();
    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    int shown = 0;
    for (const std::string &emitterName : editor->GetEmitterNames())
    {
        const ParticleCSEmitter *emitter = editor->GetEmitterByName(emitterName);
        if (!emitter || (emitter->GetFieldLayers() & f.layers) == 0)
            continue;
        ++shown;
        const bool receives = emitter->GetReceiveFields();
        const std::string label = std::format(ICON_FA_STAR " {}{}##emitter", emitterName, receives ? "" : "  （受けない設定）");
        if (!receives)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable(label.c_str(), gizmo->IsSelected(emitterName)))
        {
            if (gizmo->HasTarget(emitterName))
            {
                gizmo->SelectOnly(emitterName);
            }
        }
        if (!receives)
            ImGui::PopStyleColor();
        ImGui::SetItemTooltip("クリックでこのエミッターを選択（インスペクタで開けます）");
    }
    if (shown == 0)
    {
        DimText("なし");
    }
}

// =============================================
// 一覧の操作
// =============================================
std::string ParticleCSFieldManager::MakeUniqueFieldName(const std::string &baseName) const
{
    auto exists = [this](const std::string &name) {
        return std::any_of(fields_.begin(), fields_.end(), [&](const ParticleField &field) { return field.name == name; });
    };
    if (!exists(baseName))
    {
        return baseName;
    }
    for (int n = 1;; ++n)
    {
        const std::string candidate = baseName + "_" + std::to_string(n);
        if (!exists(candidate))
        {
            return candidate;
        }
    }
}

void ParticleCSFieldManager::DuplicateField(int index)
{
    if (index < 0 || index >= static_cast<int>(fields_.size()) || fields_.size() >= kMaxFields)
    {
        return;
    }
    ParticleField copy = fields_[index];
    copy.name = MakeUniqueFieldName(copy.name + "_copy");
    copy.spawn.timer = 0.0f;
    copy.spawn.burst = 0;
    // 重なって見分けがつかないので少しずらす
    copy.position.x += 1.0f;
    fields_.insert(fields_.begin() + index + 1, copy);
    if (soloIndex_ > index)
    {
        ++soloIndex_;
    }
    SelectField(index + 1);
    ImGuiNotification::Post("フィールドを複製しました: " + copy.name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleCSFieldManager::MoveField(int from, int to)
{
    const int count = static_cast<int>(fields_.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
    {
        return;
    }
    ParticleField moving = std::move(fields_[from]);
    fields_.erase(fields_.begin() + from);
    fields_.insert(fields_.begin() + to, std::move(moving));

    // 選択・ソロは動かした物についていく
    auto remap = [from, to](int value) {
        if (value == from)
            return to;
        if (from < to && value > from && value <= to)
            return value - 1;
        if (from > to && value >= to && value < from)
            return value + 1;
        return value;
    };
    if (selectedIndex_ >= 0)
        selectedIndex_ = remap(selectedIndex_);
    if (soloIndex_ >= 0)
        soloIndex_ = remap(soloIndex_);
}

void ParticleCSFieldManager::RemoveFieldAt(int index)
{
    if (index < 0 || index >= static_cast<int>(fields_.size()))
    {
        return;
    }
    const std::string name = fields_[index].name;
    RemoveField(index);
    const int count = static_cast<int>(fields_.size());
    if (selectedIndex_ == index)
        selectedIndex_ = (count == 0) ? -1 : (std::min)(index, count - 1);
    else if (selectedIndex_ > index)
        --selectedIndex_;
    if (soloIndex_ == index)
        soloIndex_ = -1;
    else if (soloIndex_ > index)
        --soloIndex_;
    ImGuiNotification::Post("フィールドを削除しました: " + name, {0.82f, 0.58f, 0.36f, 1.0f});
}

void ParticleCSFieldManager::SelectField(int index)
{
    if (index < 0 || index >= static_cast<int>(fields_.size()))
    {
        return;
    }
    selectedIndex_ = index;
    // ギズモの登録が追いついていない（追加した直後）ときは先に登録する
    SyncGizmoTargets();
    const std::string name = GizmoName(fields_[index].name);
    ImGuizmoManager::GetInstance()->SelectOnly(name);
    lastGizmoSelection_ = name;
}

void ParticleCSFieldManager::SyncSelectionFromGizmo()
{
    // ギズモ（シーンのアイコン・クリック）で選び直されたときだけ一覧を追従させる
    const std::unordered_set<std::string> &selected = ImGuizmoManager::GetInstance()->GetSelectedNames();
    const std::string current = (selected.size() == 1) ? *selected.begin() : std::string();
    if (current == lastGizmoSelection_)
    {
        return;
    }
    lastGizmoSelection_ = current;
    const int found = FindFieldByGizmoName(current);
    if (found >= 0)
    {
        selectedIndex_ = found;
    }
}

// =============================================
// DrawOverrideImGui
// =============================================
void ParticleCSFieldManager::DrawOverrideImGui(ParticleFieldSettingsOverride &ov, int idx)
{
#ifdef USE_IMGUI
    using namespace FieldOverrideBits;
    const std::string s = std::to_string(idx);

    // 項目ヘルパー: 先頭のチェックボックスで上書きON/OFFを切り替え、
    // ONのときだけ右隣の値エディタを操作できるようにする
    auto BitCheckbox = [&](const char *id, uint32_t bit) -> bool {
        bool checked = (ov.overrideMask & bit) != 0;
        if (ImGui::Checkbox((std::string("##cb") + id + s).c_str(), &checked))
        {
            if (checked)
                ov.overrideMask |= bit;
            else
                ov.overrideMask &= ~bit;
        }
        ImGui::SameLine();
        return checked;
    };

    ImGui::Indent(12.0f);
    ImGui::TextDisabled("チェックした項目だけ、入った粒子へ一度だけ適用されます");

    // ---- 寿命（Min/Max乱数で上書き） ----
    {
        const bool on = BitCheckbox("life", LifeTime);
        if (!on)
            ImGui::BeginDisabled();
        float v[2] = {ov.lifeTimeMin, ov.lifeTimeMax};
        if (ImGui::DragFloat2(("寿命 Min/Max##ov" + s).c_str(), v, 0.01f, 0.0f, 60.0f, "%.2f s"))
        {
            ov.lifeTimeMin = v[0];
            ov.lifeTimeMax = std::max(v[0], v[1]);
        }
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("寿命を Min〜Max の乱数で上書きします。\n短くすると入った粒子が早く消えます。");
    }

    // ---- スケール（Min/Max乱数で上書き） ----
    {
        const bool on = BitCheckbox("scale", Scale);
        if (!on)
            ImGui::BeginDisabled();
        float v[2] = {ov.scaleMin, ov.scaleMax};
        if (ImGui::DragFloat2(("スケール Min/Max##ov" + s).c_str(), v, 0.01f, 0.0f, 99.0f, "%.2f"))
        {
            ov.scaleMin = v[0];
            ov.scaleMax = std::max(v[0], v[1]);
        }
        if (!on)
            ImGui::EndDisabled();
    }

    // ---- 速度（Min/Max乱数で置換） ----
    {
        const bool on = BitCheckbox("vel", Velocity);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat3(("速度 Min##ov" + s).c_str(), &ov.velocityMin.x, 0.01f, -999.0f, 999.0f, "%.2f");
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("速度を成分ごとの Min〜Max 乱数で置き換えます");
        const float checkboxWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::Indent(checkboxWidth);
        ImGui::DragFloat3(("速度 Max##ov" + s).c_str(), &ov.velocityMax.x, 0.01f, -999.0f, 999.0f, "%.2f");
        ImGui::Unindent(checkboxWidth);
        if (!on)
            ImGui::EndDisabled();
    }

    // ---- 速度倍率（一度だけ乗算） ----
    {
        const bool on = BitCheckbox("velmul", VelocityMul);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat(("速度倍率##ov" + s).c_str(), &ov.velocityMultiplier, 0.01f, -10.0f, 10.0f, "%.2f");
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("入った瞬間に速度へ一度だけ乗算します。\n0=停止 / 0.5=減速 / 負=反転");
    }

    // ---- 加速度インパルス（一度だけ加算） ----
    {
        const bool on = BitCheckbox("impulse", AccelImpulse);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat3(("加速インパルス##ov" + s).c_str(), &ov.accelImpulse.x, 0.01f, -999.0f, 999.0f, "%.2f");
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("入った瞬間に速度へ一度だけ加算します（吹き飛ばし等）");
    }

    // ---- 色（RGBを上書きして固定） ----
    {
        const bool on = BitCheckbox("color", Color);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::ColorEdit4(("色上書き##ov" + s).c_str(), &ov.color.x, ImGuiColorEditFlags_Float);
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("入った粒子のRGBをこの色に固定します。\nアルファのフェードは通常どおり継続します。");
    }

    // ---- トレイル生成間隔の上書き ----
    {
        const bool on = BitCheckbox("traildist", TrailDistance);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat(("トレイル生成間隔##ov" + s).c_str(), &ov.trailSpawnDistance, 0.005f, 0.001f, 10.0f, "%.3f");
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("トレイルの生成間隔（距離）を上書きします。\n小さいほど濃く出ます。");
    }

    // ---- 向け替え（速さを保ったままターゲット方向へ） ----
    {
        const bool on = BitCheckbox("redirect", GatherRedirect);
        if (!on)
            ImGui::BeginDisabled();
        ImGui::DragFloat3(("向け替え先##ov" + s).c_str(), &ov.gatherTarget.x, 0.1f, -9999.0f, 9999.0f, "%.1f");
        if (!on)
            ImGui::EndDisabled();
        if (on && ImGui::IsItemHovered())
            ImGui::SetTooltip("入った瞬間、速さを保ったままこの座標の方向へ向け替えます");
    }

    ImGui::Unindent(12.0f);
#endif
}
} // namespace Hagine
#else
namespace Hagine {
void ParticleCSFieldManager::DrawImGui() {}
} // namespace Hagine
#endif // USE_IMGUI
