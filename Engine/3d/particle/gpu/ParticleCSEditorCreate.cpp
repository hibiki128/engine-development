#include "ParticleCSEditor.h"
#include <asset/AssetPath.h>
#include "DirectXCommon.h"
#include "../utility/debug/imgui/ImGuizmoManager.h"
// DebugUIHelper.h は ImGui:: を使うので imgui.h（ImGuizmoManager.h 経由）の後に include する
#include "../utility/debug/imgui/DebugUIHelper.h"
#include <data/DataHandler.h>
#include <icon/IconsFontAwesome5.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#include <algorithm>
#include <filesystem>
#include <format>
#include <map>

// GPUパーティクルエディタの「作成」「削除」タブ。
// 作成はプリセット（エンジン付属の板ポリと丸い画像だけで組む）・既存の複製・保存済みの読み込みを並べ、
// 細かく指定する従来の画面は「詳しく作る」の中へ下げる。

namespace Hagine {

namespace {
// ---------------------------------------------------------
// プリセット
// ---------------------------------------------------------
// プリセットが使うグループ（板ポリ＋エンジン付属の丸い画像）。
// 同じグループを1つのエミッターに2回ぶら下げると編集できなくなるので、層ごとに別の名前を使う
constexpr const char *kPresetTexture = "debug/circle2.png";
constexpr uint32_t kPresetGroupMaxCount = 2048;

struct PresetGroupDef
{
    const char *name;
    BlendMode blend;
};
const PresetGroupDef kPresetGroups[] = {
    {"presetGlow1", BlendMode::Add},
    {"presetGlow2", BlendMode::Add},
    {"presetGlow3", BlendMode::Add},
    {"presetSoft1", BlendMode::Normal},
};

/// <summary>プリセットの1層（どのグループを使い、どう設定するか）</summary>
struct PresetLayer
{
    const char *group;
    void (*setup)(ParticleCSGroup &group);
};

/// <summary>プリセット1つ</summary>
struct GpuPreset
{
    const char *key;   // 自動で付ける名前のもと（英字）
    const char *label; // 表示名（アイコンつき）
    const char *hint;  // 説明
    float frequency;   // 発生の間隔（秒）
    Vector3 emitterScale;
    std::vector<PresetLayer> layers;
};

// 設定の書き方をそろえるための小さな部品
void Life(ParticleCSSettings &s, float minTime, float maxTime)
{
    s.lifeTimeMin = minTime;
    s.lifeTimeMax = maxTime;
}
void Size(ParticleCSSettings &s, float minScale, float maxScale)
{
    s.scaleMin = minScale;
    s.scaleMax = maxScale;
}
void Velocity(ParticleCSSettings &s, const Vector3 &minVelocity, const Vector3 &maxVelocity)
{
    s.velocityMin = minVelocity;
    s.velocityMax = maxVelocity;
}
void Colors(ParticleCSSettings &s, const Vector4 &start, const Vector4 &end)
{
    s.startColor = start;
    s.endColor = end;
}
void EndScale(ParticleCSSettings &s, float scale)
{
    s.enableEndScale = 1;
    s.endScaleValue = {scale, scale, scale};
}
void Stretch(ParticleCSGroup &g, float factor)
{
    g.GetPerView()->enableVelocityStretch = 1;
    g.GetPerView()->velocityStretchFactor = factor;
    g.GetSettingsData()->frustumStretchFactor = factor;
}
const std::vector<GpuPreset> &Presets()
{
    static const std::vector<GpuPreset> kPresets = {
        {"fire", ICON_FA_FIRE " 炎", "たき火のように立ち上る炎と、舞い上がる火の粉", 0.05f, {0.6f, 0.2f, 0.6f},
         {
             {"presetGlow1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 3;
                  Life(s, 0.5f, 0.9f);
                  Size(s, 0.7f, 1.1f);
                  Velocity(s, {-0.3f, 1.8f, -0.3f}, {0.3f, 3.2f, 0.3f});
                  Colors(s, {1.0f, 0.55f, 0.15f, 0.45f}, {0.8f, 0.1f, 0.02f, 0.0f});
                  s.enableMidColor = 1;
                  s.midColorRatio = 0.35f;
                  s.midColor = {1.0f, 0.35f, 0.08f, 0.35f};
                  EndScale(s, 0.15f);
                  g.SetEmissive(1.6f);
              }},
             {"presetGlow2",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 1;
                  Life(s, 0.8f, 1.4f);
                  Size(s, 0.08f, 0.15f);
                  Velocity(s, {-0.8f, 2.0f, -0.8f}, {0.8f, 4.5f, 0.8f});
                  Colors(s, {1.0f, 0.8f, 0.4f, 1.0f}, {1.0f, 0.3f, 0.1f, 0.0f});
                  s.enableTurbulence = 1;
                  s.turbulenceStrength = 2.0f;
                  g.SetEmissive(3.0f);
              }},
         }},
        {"smoke", ICON_FA_CLOUD " 煙", "ゆっくり昇りながら広がって消える煙", 0.08f, {0.5f, 0.1f, 0.5f},
         {
             {"presetSoft1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 1;
                  Life(s, 2.0f, 3.0f);
                  Size(s, 0.8f, 1.2f);
                  Velocity(s, {-0.3f, 0.6f, -0.3f}, {0.3f, 1.2f, 0.3f});
                  Colors(s, {0.85f, 0.85f, 0.88f, 0.35f}, {0.6f, 0.6f, 0.64f, 0.0f});
                  EndScale(s, 3.0f);
                  s.enableRandomRotation = 1;
                  s.rotationMax = {0.0f, 0.0f, 6.28f};
                  s.enableTurbulence = 1;
                  s.turbulenceStrength = 0.5f;
                  s.enableSoftParticle = 1;
                  g.SetEmissive(1.0f);
              }},
         }},
        {"sparks", ICON_FA_STAR " 火花", "はじけて飛び散り、重力で落ちる火花", 0.6f, {0.1f, 0.1f, 0.1f},
         {
             {"presetGlow1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 14;
                  Life(s, 0.3f, 0.6f);
                  Size(s, 0.09f, 0.14f);
                  Velocity(s, {-6.0f, 1.0f, -6.0f}, {6.0f, 9.0f, 6.0f});
                  Colors(s, {1.0f, 0.95f, 0.6f, 1.0f}, {1.0f, 0.4f, 0.1f, 0.0f});
                  s.enableGravity = 1;
                  s.gravity = {0.0f, -15.0f, 0.0f};
                  Stretch(g, 0.12f);
                  g.SetEmissive(4.0f);
              }},
         }},
        {"aura", ICON_FA_BURN " オーラ", "体を包んで立ち上る気のオーラ（キャラの足元に置く）", 0.04f, {0.8f, 1.5f, 0.8f},
         {
             {"presetGlow1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 4;
                  Life(s, 0.35f, 0.6f);
                  Size(s, 0.5f, 0.8f);
                  Velocity(s, {-0.2f, 4.0f, -0.2f}, {0.2f, 7.0f, 0.2f});
                  Colors(s, {1.0f, 0.85f, 0.4f, 0.28f}, {1.0f, 0.55f, 0.12f, 0.0f});
                  Stretch(g, 0.08f);
                  g.SetEmissive(1.8f);
              }},
             {"presetGlow2",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 1;
                  Life(s, 0.6f, 1.0f);
                  Size(s, 0.06f, 0.1f);
                  Velocity(s, {0.0f, 4.0f, 0.0f}, {0.0f, 8.0f, 0.0f});
                  Colors(s, {1.0f, 0.95f, 0.7f, 1.0f}, {1.0f, 0.8f, 0.3f, 0.0f});
                  Stretch(g, 0.06f);
                  g.SetEmissive(3.0f);
              }},
         }},
        {"explosion", ICON_FA_BOMB " 爆発", "閃光・火の玉・火花・煙を一度に出す（1秒ごとにくり返す）", 1.0f, {0.3f, 0.3f, 0.3f},
         {
             {"presetGlow1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 1;
                  Life(s, 0.15f, 0.15f);
                  Size(s, 2.0f, 2.0f);
                  Velocity(s, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
                  Colors(s, {1.0f, 0.95f, 0.8f, 1.0f}, {1.0f, 0.6f, 0.2f, 0.0f});
                  EndScale(s, 5.0f);
                  g.SetEmissive(5.0f);
              }},
             {"presetGlow2",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 10;
                  Life(s, 0.5f, 0.9f);
                  Size(s, 0.9f, 1.5f);
                  Velocity(s, {-7.0f, -2.0f, -7.0f}, {7.0f, 8.0f, 7.0f});
                  Colors(s, {1.0f, 0.8f, 0.3f, 1.0f}, {0.8f, 0.2f, 0.05f, 0.0f});
                  s.enableVelocityDamping = 1;
                  s.velocityDampingFactor = 0.92f; // 1フレームごとに掛ける倍率
                  EndScale(s, 0.2f);
                  g.SetEmissive(3.0f);
              }},
             {"presetGlow3",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 16;
                  Life(s, 0.4f, 0.8f);
                  Size(s, 0.05f, 0.08f);
                  Velocity(s, {-12.0f, -2.0f, -12.0f}, {12.0f, 14.0f, 12.0f});
                  Colors(s, {1.0f, 0.95f, 0.6f, 1.0f}, {1.0f, 0.5f, 0.1f, 0.0f});
                  s.enableGravity = 1;
                  s.gravity = {0.0f, -20.0f, 0.0f};
                  Stretch(g, 0.06f);
                  g.SetEmissive(4.0f);
              }},
             {"presetSoft1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 6;
                  Life(s, 1.5f, 2.5f);
                  Size(s, 1.0f, 1.6f);
                  Velocity(s, {-2.0f, 0.5f, -2.0f}, {2.0f, 3.0f, 2.0f});
                  Colors(s, {0.6f, 0.56f, 0.52f, 0.5f}, {0.4f, 0.4f, 0.4f, 0.0f});
                  s.enableVelocityDamping = 1;
                  s.velocityDampingFactor = 0.96f; // 1フレームごとに掛ける倍率
                  EndScale(s, 3.0f);
                  s.enableSoftParticle = 1;
                  g.SetEmissive(1.0f);
              }},
         }},
        {"trail", ICON_FA_MAGIC " 軌跡", "尾を引いて飛ぶ光の玉（流れ星・魔法弾）", 0.25f, {0.1f, 0.1f, 0.1f},
         {
             {"presetGlow1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 1;
                  Life(s, 1.2f, 1.2f);
                  Size(s, 0.25f, 0.25f);
                  Velocity(s, {-3.0f, 6.0f, -3.0f}, {3.0f, 9.0f, 3.0f});
                  Colors(s, {0.6f, 0.85f, 1.0f, 1.0f}, {0.3f, 0.5f, 1.0f, 0.0f});
                  s.enableGravity = 1;
                  s.gravity = {0.0f, -6.0f, 0.0f};
                  s.enableTrail = 1;
                  s.trailSpawnDistance = 0.15f;
                  s.maxTrailPerParticle = 20;
                  s.trailLifeTimeScale = 0.4f;
                  s.trailColorMultiplier = {1.0f, 1.0f, 1.0f, 0.6f};
                  g.SetEmissive(3.0f);
              }},
         }},
        {"shockwave", ICON_FA_DOT_CIRCLE " 衝撃波", "広がって消える輪（着地・着弾）", 1.0f, {0.1f, 0.1f, 0.1f},
         {
             {"presetGlow1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 1;
                  Life(s, 0.5f, 0.5f);
                  Size(s, 0.5f, 0.5f);
                  Velocity(s, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
                  Colors(s, {0.8f, 0.9f, 1.0f, 1.0f}, {0.5f, 0.7f, 1.0f, 0.0f});
                  EndScale(s, 6.0f);
                  s.shapeMode = 3;
                  s.shapeEdge = 0.35f;
                  s.shapeSoftness = 0.1f;
                  s.shapeNoiseScale = 1.0f;
                  s.shapeRimWidth = 0.08f;
                  s.shapeRimColor = {0.5f, 0.8f, 1.0f, 1.0f};
                  g.SetEmissive(2.0f);
              }},
         }},
        {"lightning", ICON_FA_BOLT " 稲妻", "ぱちぱちと走る放電", 0.12f, {0.6f, 0.6f, 0.6f},
         {
             {"presetGlow1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 4;
                  Life(s, 0.1f, 0.16f);
                  Size(s, 0.6f, 1.0f);
                  Velocity(s, {-6.0f, -6.0f, -6.0f}, {6.0f, 6.0f, 6.0f});
                  Colors(s, {0.7f, 0.85f, 1.0f, 1.0f}, {0.3f, 0.5f, 1.0f, 0.0f});
                  s.shapeMode = 2;
                  s.shapeEdge = 0.5f;
                  s.shapeSoftness = 0.02f;
                  s.shapeNoiseScale = 5.0f;
                  s.shapeRimWidth = 0.3f;
                  s.shapeRimColor = {0.3f, 0.6f, 1.0f, 1.0f};
                  Stretch(g, 0.05f);
                  g.SetEmissive(2.8f);
              }},
         }},
        {"sparkle", ICON_FA_GEM " キラキラ", "広い範囲でまたたく光の粒", 0.05f, {3.0f, 2.0f, 3.0f},
         {
             {"presetGlow1",
              [](ParticleCSGroup &g) {
                  ParticleCSSettings &s = *g.GetSettingsData();
                  s.emitCount = 3;
                  Life(s, 0.6f, 1.2f);
                  Size(s, 0.05f, 0.12f);
                  Velocity(s, {-0.3f, -0.1f, -0.3f}, {0.3f, 0.5f, 0.3f});
                  Colors(s, {0.8f, 0.9f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f});
                  s.enableSinScale = 1;
                  s.sinScaleFrequency = 10.0f;
                  s.sinScaleAmplitude = 0.5f;
                  g.SetEmissive(3.0f);
              }},
         }},
    };
    return kPresets;
}

/// <summary>プリセットが使うグループが無ければ作る（作ると保存ファイルもできる）</summary>
bool EnsurePresetGroup(const char *name)
{
    ParticleCSGroupManager *pManager = ParticleCSGroupManager::GetInstance();
    if (pManager->GetParticleCSGroup(name))
        return true;
    for (const PresetGroupDef &def : kPresetGroups)
    {
        if (std::string(def.name) == name)
        {
            pManager->CreatePrimitiveParticleCSGroup(def.name, PrimitiveType::Plane, kPresetGroupMaxCount, kPresetTexture, def.blend);
            return pManager->GetParticleCSGroup(name) != nullptr;
        }
    }
    return false;
}
} // namespace

bool ParticleCSEditor::CreateEmitterFromPreset(const std::string &name, int presetIndex)
{
    const auto &presets = Presets();
    if (presetIndex < 0 || presetIndex >= static_cast<int>(presets.size()) || emitters_.contains(name))
        return false;
    const GpuPreset &preset = presets[presetIndex];

    AddParticleEmitter(name);
    ParticleCSEmitter *emitter = emitters_[name].get();
    for (const PresetLayer &layer : preset.layers)
    {
        if (!EnsurePresetGroup(layer.group))
        {
            ImGuiNotification::Post(std::string("プリセットのグループを作れませんでした: ") + layer.group, {0.8f, 0.46f, 0.46f, 1.0f});
            continue;
        }
        const size_t before = emitter->GetParticleGroups().size();
        emitter->AddParticleGroup(ParticleCSGroupManager::GetInstance()->GetParticleCSGroup(layer.group));
        if (emitter->GetParticleGroups().size() == before)
            continue;
        ParticleCSGroup *group = emitter->GetParticleGroups().back();
        ParticleCSSettings clean{};
        clean.maxParticleCount = group->GetSettingsData()->maxParticleCount;
        *group->GetSettingsData() = clean;
        group->SetEmissive(1.0f);
        group->SetBillboard(true);
        layer.setup(*group);
    }
    emitter->SetFrequency(preset.frequency);
    emitter->SetScale(preset.emitterScale);
    emitter->SetTranslate({0.0f, 0.0f, 0.0f});
    emitter->SetAuto(true);

    // 作ったらすぐプレビューに出す
    selectedEmitterName_ = name;
    emitterSearch_.clear();
    ImGuiNotification::Post(std::format("プリセット「{}」から作りました: {}（保存はエミッターの設定から）", preset.label, name), {0.45f, 0.68f, 0.52f, 1.0f});
    return true;
}

bool ParticleCSEditor::DuplicateEmitter(const std::string &source, const std::string &name)
{
    auto it = emitters_.find(source);
    if (it == emitters_.end() || !it->second || emitters_.contains(name))
        return false;

    // 今の設定（未保存の調整も含む）を新しい名前の保存ファイルへ書き、それを読み込んで作る
    {
        ImGuiNotification::ScopedMute mute;
        ParticleCSEmitter *src = it->second.get();
        src->SetName(name);
        src->SaveSetting();
        src->SetName(source);
    }
    AddParticleEmitter(name);
    selectedEmitterName_ = name;
    emitterSearch_.clear();
    ImGuiNotification::Post(std::format("「{}」を複製しました: {}", source, name), {0.45f, 0.68f, 0.52f, 1.0f});
    return true;
}

void ParticleCSEditor::DrawQuickCreate()
{
#ifdef USE_IMGUI
    const std::string jsonDir = AssetPath::Json("ParticleCS");
    const auto isLoaded = [this](const std::string &n) { return emitters_.contains(n); };

    // ---- 名前 ----
    ImGui::SeparatorText(ICON_FA_TAG " 名前");
    const ParticleEditorUI::NameCheck nameCheck =
        ParticleEditorUI::DrawNameField("##quickName", quickName_, "空のままならプリセット名から自動で付けます", jsonDir, isLoaded);
    const bool nameUsable = quickName_.empty() || nameCheck.ok;

    // ---- プリセット ----
    ImGui::SeparatorText(ICON_FA_MAGIC " プリセットから作る");
    const auto &presets = Presets();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    ImGui::BeginDisabled(!nameUsable);
    for (int i = 0; i < static_cast<int>(presets.size()); ++i)
    {
        if (i % 3 != 0)
            ImGui::SameLine();
        if (NeutralButton(presets[i].label, ImVec2(buttonWidth, 0.0f)))
        {
            const std::string name = quickName_.empty() ? ParticleEditorUI::MakeUniqueEmitterName(presets[i].key, jsonDir, isLoaded) : quickName_;
            if (CreateEmitterFromPreset(name, i))
                quickName_.clear();
        }
        ImGui::SetItemTooltip("%s\n層の数: %zu（エミッターの設定で1つずつ直せます）", presets[i].hint, presets[i].layers.size());
    }
    ImGui::EndDisabled();
    DimText("作るとプレビューにすぐ出ます。気に入ったらエミッターの設定から保存してください");

    // ---- 複製 ----
    ImGui::SeparatorText(ICON_FA_COPY " 今あるエミッターを複製");
    if (emitters_.empty())
    {
        DimText("エミッターがありません");
    }
    else
    {
        if (!emitters_.contains(duplicateSource_))
            duplicateSource_.clear();
        if (duplicateSource_.empty() && emitters_.contains(selectedEmitterName_))
            duplicateSource_ = selectedEmitterName_;
        std::vector<std::string> names = GetEmitterNames();
        std::sort(names.begin(), names.end());
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("  複製  ").x - spacing * 2.0f);
        if (ImGui::BeginCombo("##dupSource", duplicateSource_.empty() ? "元にするエミッター" : duplicateSource_.c_str()))
        {
            for (const std::string &n : names)
            {
                if (ImGui::Selectable(n.c_str(), n == duplicateSource_))
                    duplicateSource_ = n;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(duplicateSource_.empty() || !nameUsable);
        if (PrimaryButton(" 複製 "))
        {
            const std::string name =
                quickName_.empty() ? ParticleEditorUI::MakeUniqueEmitterName(duplicateSource_ + "_copy", jsonDir, isLoaded) : quickName_;
            if (DuplicateEmitter(duplicateSource_, name))
                quickName_.clear();
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("今の設定（保存していない調整も含む）をそのまま写して、新しい名前で保存します");
    }

    // ---- 保存済みから読み込む ----
    ImGui::SeparatorText(ICON_FA_FOLDER_OPEN " 保存済みから読み込む");
    ParticleEditorUI::DrawSavedFileLoader("gpuLoad", loadSearch_, jsonDir, isLoaded, [this](const std::string &n) {
        AddParticleEmitter(n);
        selectedEmitterName_ = n;
    });

    ImGui::Spacing();
    if (NeutralButton(ICON_FA_STOP " すべての自動発生を止める"))
    {
        for (auto &emitter : emitters_)
        {
            emitter.second->SetAuto(false);
        }
    }
#endif // USE_IMGUI
}

// エミッターとパーティクルグループの一覧表示・削除UIを描画する
void ParticleCSEditor::ShowDeleteSection()
{
#ifdef USE_IMGUI
    const std::string jsonDir = AssetPath::Json("ParticleCS");

    ImGui::SeparatorText(ICON_FA_BOLT " エミッター");
    ParticleEditorUI::DrawEmitterDeleteList(
        "gpuEmitterDelete", deleteState_, GetEmitterNames(), jsonDir,
        [this](const std::string &n) { RemoveParticleEmitter(n); },
        [this](const std::string &n) {
            if (!emitters_.contains(n))
            {
                AddParticleEmitter(n);
                selectedEmitterName_ = n;
            }
        },
        [this](const std::string &n) {
            auto it = emitters_.find(n);
            if (it == emitters_.end() || !it->second)
                return std::string();
            return std::format("グループ {} 個 / {}", it->second->GetParticleGroups().size(), it->second->GetAuto() ? "自動発生中" : "止まっている");
        });

    // ---- グループ（粒の形の定義。消すとファイルも消える）----
    ImGui::Spacing();
    ImGui::SeparatorText(ICON_FA_LAYER_GROUP " グループ（粒の形の定義）");
    std::vector<std::string> groupNames = pParticleGroupManager_->GetAllGroupNames();
    if (groupNames.empty())
    {
        DimText("グループがありません");
        return;
    }
    // どのエミッターが使っているかを数えておく（使われているグループを消すと、そのエミッターから層が抜ける）
    std::map<std::string, int> usage;
    for (const auto &[name, emitter] : emitters_)
    {
        if (!emitter)
            continue;
        for (ParticleCSGroup *group : emitter->GetParticleGroups())
        {
            if (group)
                ++usage[group->GetGroupName()];
        }
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##groupSearch", ICON_FA_SEARCH " グループを名前で絞り込み", &groupSearch_);
    ImGui::BeginChild("##groupList", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 6.5f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    for (const std::string &name : groupNames)
    {
        if (!groupSearch_.empty() && name.find(groupSearch_) == std::string::npos)
            continue;
        ImGui::PushID(name.c_str());
        if (ImGui::Selectable(name.c_str(), selectedGroupName_ == name))
            selectedGroupName_ = name;
        const int used = usage.contains(name) ? usage[name] : 0;
        if (used > 0)
        {
            ImGui::SameLine();
            ImGui::TextColored(DebugTheme::kAccentYellow, "(%d 個のエミッターが使用中)", used);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    const bool hasGroup = std::find(groupNames.begin(), groupNames.end(), selectedGroupName_) != groupNames.end();
    ImGui::BeginDisabled(!hasGroup);
    if (DangerButton(ICON_FA_TRASH " グループを削除する...", ImVec2(-1.0f, 0.0f)))
    {
        ImGui::OpenPopup("##confirmGroupDelete");
    }
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("##confirmGroupDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const int used = usage.contains(selectedGroupName_) ? usage[selectedGroupName_] : 0;
        ImGui::Text("グループ「%s」を保存ファイルごと削除します。", selectedGroupName_.c_str());
        if (used > 0)
        {
            ImGui::TextColored(DebugTheme::kAccentOrange, ICON_FA_EXCLAMATION_TRIANGLE " %d 個のエミッターが使っています。次に読み込むとその層は無くなります", used);
        }
        ImGui::Spacing();
        if (DangerButton(ICON_FA_TRASH " 削除", ImVec2(120.0f, 0.0f)))
        {
            const std::string name = selectedGroupName_;
            // 元に戻せるよう、定義（形・画像・数・合成）を覚えておく
            DataHandler data("ParticleCSGroup", name);
            const std::string texture = data.Load<std::string>("textureName", "");
            const std::string model = data.Load<std::string>("modelFilePath", "");
            const uint32_t maxCount = data.Load<uint32_t>("maxParticleCount", 10000);
            const BlendMode blend = data.Load<BlendMode>("blendMode", BlendMode::Add);
            const PrimitiveType primitive = data.Load<PrimitiveType>("primitiveType", PrimitiveType::None);

            DirectXCommon::GetInstance()->WaitForGPU(); // 使用中かもしれない粒のバッファを消すので、描画が終わるのを待つ
            {
                // 先にエミッターから外す（外さないと、消えたグループをエミッターが指したままになる）
                ImGuiNotification::ScopedMute mute;
                ParticleCSEmitter::DetachGroupFromAll(name);
            }
            pParticleGroupManager_->RemoveParticleCSGroup(name);
            data.DeleteJson(name);
            selectedGroupName_.clear();
            ImGuiNotification::PostWithAction("グループを削除しました: " + name, {0.82f, 0.58f, 0.36f, 1.0f}, "元に戻す",
                                              [name, texture, model, maxCount, blend, primitive] {
                                                  ParticleCSGroupManager *pManager = ParticleCSGroupManager::GetInstance();
                                                  if (!model.empty())
                                                      pManager->CreateParticleCSGroup(name, model, maxCount, texture, blend);
                                                  else
                                                      pManager->CreatePrimitiveParticleCSGroup(name, primitive, maxCount, texture, blend);
                                              });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("やめる", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#endif // USE_IMGUI
}

} // namespace Hagine
