#define NOMINMAX
#include "ParticleCSGroup.h"
#include <asset/AssetPath.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <line/LineRenderer.h>
#ifdef USE_IMGUI
#include <implot.h>
// ※ namespace Hagine の外で include すること。ImGradient.h は `struct ImVec4;` を前方宣言するため、
//   namespace 内で include すると Hagine::ImVec4(不完全型) が生成され全 ImVec4 参照が壊れる。
#include "imgui.h"
#include "ImGradient.h"
#include "ImCurveEdit.h"
#include "utility/debug/imgui/AssetDragDrop.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "ParticleCSGroupImGuiInternal.h"
#endif

// ParticleCSGroup のエディタUI: エフェクトカード（追加したものだけ表示する分）。
// 発生形状と場系（ギャザー／渦／カールノイズ／トレイル）は ParticleCSGroupImGuiField.cpp、
// DrawImGui 本体と基本セクションは ParticleCSGroupImGui.cpp にある。
namespace Hagine {
#ifdef USE_IMGUI

namespace {
// ImCurveEdit 用デリゲート。サイズ(0)/アルファ(1) の倍率カーブを1つのエディタで編集する。
// 点の実体は group の sizeCurvePoints_/alphaCurvePoints_(CurvePoint列)。ImVec2 スクラッチ経由で編集する。
struct LifetimeCurvesDelegate : public ImCurveEdit::Delegate
{
    std::vector<Hagine::CurvePoint> *pts[2] = {nullptr, nullptr}; // 0=size, 1=alpha
    std::vector<ImVec2> scratch[2];
    bool visible[2] = {true, true};
    bool changed = false; // この Edit 呼び出しで点が編集されたか（dirty 判定用）
    ImVec2 vmin = ImVec2(0.0f, 0.0f);
    ImVec2 vmax = ImVec2(1.0f, 2.0f);
    void Sync()
    {
        for (int c = 0; c < 2; ++c)
        {
            scratch[c].clear();
            if (pts[c])
                for (const auto &p : *pts[c])
                    scratch[c].push_back(ImVec2(p.x, p.y));
        }
    }
    size_t GetCurveCount() override { return 2; }
    bool IsVisible(size_t c) override { return c < 2 ? visible[c] : true; }
    ImCurveEdit::CurveType GetCurveType(size_t) const override { return ImCurveEdit::CurveLinear; }
    ImVec2 &GetMin() override { return vmin; }
    ImVec2 &GetMax() override { return vmax; }
    size_t GetPointCount(size_t c) override { return (c < 2 && pts[c]) ? pts[c]->size() : 0; }
    uint32_t GetCurveColor(size_t c) override { return c == 0 ? 0xFF3399FF : 0xFFFFCC66; } // size=橙 / alpha=水(ABGR)
    ImVec2 *GetPoints(size_t c) override { return c < 2 ? scratch[c].data() : nullptr; }
    int EditPoint(size_t c, int index, ImVec2 value) override
    {
        if (c >= 2 || !pts[c] || index < 0 || index >= static_cast<int>(pts[c]->size()))
            return index;
        value.x = value.x < 0.0f ? 0.0f : (value.x > 1.0f ? 1.0f : value.x);
        if (value.y < 0.0f)
            value.y = 0.0f;
        (*pts[c])[index] = {value.x, value.y};
        scratch[c][index] = value;
        changed = true;
        while (index > 0 && (*pts[c])[index].x < (*pts[c])[index - 1].x)
        {
            std::swap((*pts[c])[index], (*pts[c])[index - 1]);
            std::swap(scratch[c][index], scratch[c][index - 1]);
            --index;
        }
        while (index < static_cast<int>(pts[c]->size()) - 1 && (*pts[c])[index].x > (*pts[c])[index + 1].x)
        {
            std::swap((*pts[c])[index], (*pts[c])[index + 1]);
            std::swap(scratch[c][index], scratch[c][index + 1]);
            ++index;
        }
        return index;
    }
    void AddPoint(size_t c, ImVec2 value) override
    {
        if (c >= 2 || !pts[c])
            return;
        value.x = value.x < 0.0f ? 0.0f : (value.x > 1.0f ? 1.0f : value.x);
        if (value.y < 0.0f)
            value.y = 0.0f;
        pts[c]->push_back({value.x, value.y});
        std::sort(pts[c]->begin(), pts[c]->end(),
                  [](const Hagine::CurvePoint &a, const Hagine::CurvePoint &b) { return a.x < b.x; });
        changed = true;
        Sync();
    }
};
} // namespace

void ParticleCSGroup::DrawImGuiEffectSection()
{
    // =======================================================
    // エフェクト（追加したものだけカード表示）
    // =======================================================
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
    ImGui::TextUnformatted("エフェクト");
    ImGui::PopStyleColor();
    {
        // 「＋追加」リスト。各エフェクトの「追加済みか」と「追加アクション」を列挙する。
        // グループ系（描画カリング/寿命カーブ/回転）は内部フラグのいずれかが立っていれば追加済み扱い。
        struct AddItem
        {
            const char *name;
            bool added;
            std::function<void()> add;
        };
        std::vector<AddItem> addItems = {
            {"重力", pSettingsData_->enableGravity != 0, [&] { pSettingsData_->enableGravity = 1; }},
            {"加速度", pSettingsData_->enableAcceleration != 0, [&] { pSettingsData_->enableAcceleration = 1; }},
            {"速度減衰", pSettingsData_->enableVelocityDamping != 0, [&] { pSettingsData_->enableVelocityDamping = 1; }},
            {"寿命による速度減衰", pSettingsData_->enableLifetimeVelocityDamping != 0, [&] { pSettingsData_->enableLifetimeVelocityDamping = 1; }},
            {"寿命で縮小", pSettingsData_->enableLifetimeScale != 0, [&] { pSettingsData_->enableLifetimeScale = 1; }},
            {"Sin波で拡縮", pSettingsData_->enableSinScale != 0, [&] { pSettingsData_->enableSinScale = 1; }},
            {"速度ストレッチ", pPerViewData_->enableVelocityStretch != 0, [&] { pPerViewData_->enableVelocityStretch = 1; }},
            {"描画カリング", (pPerViewData_->enableDistanceCull || pPerViewData_->enableSizeClamp) != 0, [&] { pPerViewData_->enableDistanceCull = 1; }},
            {"寿命カーブ", (pSettingsData_->enableSizeCurve || pSettingsData_->enableAlphaCurve) != 0, [&] { pSettingsData_->enableSizeCurve = 1; MarkLifeCurvesDirty(); }},
            {"タービュランス", pSettingsData_->enableTurbulence != 0, [&] { pSettingsData_->enableTurbulence = 1; }},
            {"音声振動", pSettingsData_->enableAudioVibration != 0, [&] { pSettingsData_->enableAudioVibration = 1; }},
            {"終了スケール", pSettingsData_->enableEndScale != 0, [&] { pSettingsData_->enableEndScale = 1; }},
            {"回転", (pSettingsData_->enableRandomRotation || pSettingsData_->enableRandomAngularVelocity) != 0, [&] { pSettingsData_->enableRandomRotation = 1; }},
            {"放射状速度", pSettingsData_->enableRadialVelocity != 0, [&] { pSettingsData_->enableRadialVelocity = 1; }},
            {"ギャザー", pSettingsData_->enableGather != 0, [&] { pSettingsData_->enableGather = 1; }},
            {"渦巻き", pSettingsData_->enableVortex != 0, [&] { pSettingsData_->enableVortex = 1; }},
            {"カールノイズ", pSettingsData_->enableCurlNoise != 0, [&] { pSettingsData_->enableCurlNoise = 1; }},
            {"トレイル", pSettingsData_->enableTrail != 0, [&] { pSettingsData_->enableTrail = 1; }},
        };
        int notAdded = 0;
        for (const auto &it : addItems)
            if (!it.added)
                ++notAdded;
        ImGui::SetNextItemWidth(-1.0f);
        const char *preview = notAdded > 0 ? "＋ エフェクトを追加..." : "（すべて追加済み）";
        if (ImGui::BeginCombo("##addEffect", preview))
        {
            for (const auto &it : addItems)
            {
                if (it.added)
                    continue;
                if (ImGui::Selectable(it.name))
                    it.add();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Spacing();

    const ImVec4 kMotionColor = DebugTheme::kAccentYellow;

    // ---- 寿命で縮小 ----
    if (pSettingsData_->enableLifetimeScale)
    {
        if (EffectHeader("寿命で縮小", kMotionColor, [&] { pSettingsData_->enableLifetimeScale = 0; }))
        {
            ImGui::Indent();
            ImGui::TextDisabled("時間経過と共にスケールが 0 に近づきます（パラメータなし）");
            ImGui::Unindent();
        }
    }

    // ---- Sin波で拡縮 ----
    if (pSettingsData_->enableSinScale)
    {
        if (EffectHeader("Sin波で拡縮", kMotionColor, [&] { pSettingsData_->enableSinScale = 0; }))
        {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));
            ImGui::DragFloat("周波数##sf", &pSettingsData_->sinScaleFrequency, 0.1f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat("振幅##sa", &pSettingsData_->sinScaleAmplitude, 0.01f, 0.0f, 999.0f, "%.4f");
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
    }

    // ---- 重力 ----
    if (pSettingsData_->enableGravity)
    {
        if (EffectHeader("重力", kMotionColor, [&] { pSettingsData_->enableGravity = 0; }))
        {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));
            ImGui::DragFloat3("重力ベクトル", &pSettingsData_->gravity.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
    }

    // ---- 加速度 ----
    if (pSettingsData_->enableAcceleration)
    {
        if (EffectHeader("加速度", kMotionColor, [&] { pSettingsData_->enableAcceleration = 0; }))
        {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));
            ImGui::DragFloat3("加速度ベクトル", &pSettingsData_->acceleration.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();
            ImGui::TextDisabled("重力とは別に毎フレーム速度に加算されます");
            ImGui::Unindent();
        }
    }

    // ---- 速度減衰 ----
    if (pSettingsData_->enableVelocityDamping)
    {
        if (EffectHeader("速度減衰", kMotionColor, [&] { pSettingsData_->enableVelocityDamping = 0; }))
        {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));
            ImGui::DragFloat("減衰係数##vd", &pSettingsData_->velocityDampingFactor, 0.001f, 0.0f, 1.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("空気抵抗のように徐々に減速します\n推奨: 0.95-0.99");
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
    }

    // ---- 寿命による速度減衰 ----
    if (pSettingsData_->enableLifetimeVelocityDamping)
    {
        if (EffectHeader("寿命による速度減衰", kMotionColor, [&] { pSettingsData_->enableLifetimeVelocityDamping = 0; }))
        {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));
            ImGui::DragFloat("開始タイミング##ld", &pSettingsData_->lifetimeVelocityDampingStart, 0.01f, 0.0f, 1.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("寿命末期に速度が 0 に近づきます\n0.0=最初から / 1.0=最後のみ / 推奨: 0.5-0.8");
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
    }

    // ---- 速度ストレッチ ----
    if (pPerViewData_->enableVelocityStretch)
    {
        if (EffectHeader("速度ストレッチ", kMotionColor, [&] { pPerViewData_->enableVelocityStretch = 0; }))
        {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));
            ImGui::DragFloat("ストレッチ係数##vsf", &pPerViewData_->velocityStretchFactor, 0.01f, 0.0f, 10.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("パーティクルを速度方向に引き伸ばします\n速さ × 係数 = 伸び率 / 推奨: 0.05〜0.5");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("火花##vsPre1"))
            {
                pPerViewData_->enableVelocityStretch = 1;
                pPerViewData_->velocityStretchFactor = 0.15f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("銃弾##vsPre2"))
            {
                pPerViewData_->enableVelocityStretch = 1;
                pPerViewData_->velocityStretchFactor = 0.5f;
            }
            ImGui::Unindent();
        }
    }

    // ---- 描画カリング（距離カリング / 画面サイズ制限）----
    if ((pPerViewData_->enableDistanceCull || pPerViewData_->enableSizeClamp) &&
        EffectHeader("描画カリング（overdraw対策）", DebugTheme::kAccentCyan,
                     [&] { pPerViewData_->enableDistanceCull = 0; pPerViewData_->enableSizeClamp = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);

        // 距離カリング + 距離フェード（遠い粒子のフィルレートを節約）
        {
            bool v = pPerViewData_->enableDistanceCull != 0;
            if (ImGui::Checkbox("距離カリング##dc", &v))
                pPerViewData_->enableDistanceCull = v ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("遠い粒子をアルファフェード→縮退カリングして\n半透明の重なり(ROP/blend)を減らします");
            if (v)
            {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
                ImGui::DragFloat("フェード開始距離##dcs", &pPerViewData_->distanceCullStart, 0.5f, 0.0f, 100000.0f, "%.2f");
                ImGui::DragFloat("カリング距離##dce", &pPerViewData_->distanceCullEnd, 0.5f, 0.0f, 100000.0f, "%.2f");
                ImGui::PopStyleColor();
                // 開始 <= カリング距離 を保証（フェード範囲が負にならないように）
                if (pPerViewData_->distanceCullEnd < pPerViewData_->distanceCullStart)
                    pPerViewData_->distanceCullEnd = pPerViewData_->distanceCullStart;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("開始距離からアルファをフェードし、カリング距離で完全に消えます\nカメラからの距離(ワールド単位)");
                ImGui::Unindent();
            }
        }

        ImGui::Spacing();

        // 画面サイズ上限 + 微小カリング
        {
            bool v = pPerViewData_->enableSizeClamp != 0;
            if (ImGui::Checkbox("画面サイズ制限##sc", &v))
                pPerViewData_->enableSizeClamp = v ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("巨大粒子のサイズを画面上で上限クランプし、\nサブピクセル粒子を破棄してフィルレートを節約します");
            if (v)
            {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
                ImGui::DragFloat("最大画面高さ##scmax", &pPerViewData_->maxScreenHeight, 0.01f, 0.01f, 2.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("画面上の最大高さ(NDC)。2.0=画面全体, 1.0=画面の半分\nこれを超える巨大粒子はスケールを縮小します");
                ImGui::DragFloat("微小カリング高さ##scmin", &pPerViewData_->minScreenHeight, 0.0005f, 0.0f, 0.5f, "%.4f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("画面上の高さがこれ未満の粒子を破棄(0=無効)\n例: 0.002 ≒ 1080pで約2px");
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- 寿命カーブ（サイズ/アルファ。1つのカーブエディタを共有）----
    if ((pSettingsData_->enableSizeCurve || pSettingsData_->enableAlphaCurve) &&
        EffectHeader("寿命カーブ（サイズ/アルファ）", DebugTheme::kAccentPurple,
                     [&] { pSettingsData_->enableSizeCurve = 0; pSettingsData_->enableAlphaCurve = 0; MarkLifeCurvesDirty(); }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentPurple);

        bool sizeOn = pSettingsData_->enableSizeCurve != 0;
        if (ImGui::Checkbox("サイズ倍率##szc", &sizeOn))
        {
            pSettingsData_->enableSizeCurve = sizeOn ? 1 : 0;
            MarkLifeCurvesDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("寿命に沿ってサイズを倍率(0〜2)で変化させる\n例: 0→大きく→0 でポップ感");
        ImGui::SameLine();
        bool alphaOn = pSettingsData_->enableAlphaCurve != 0;
        if (ImGui::Checkbox("アルファ倍率##alc", &alphaOn))
        {
            pSettingsData_->enableAlphaCurve = alphaOn ? 1 : 0;
            MarkLifeCurvesDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("寿命に沿って不透明度を倍率で変化させる\n例: フェードイン→アウト");

        if (sizeOn || alphaOn)
        {
            LifetimeCurvesDelegate dg;
            dg.pts[0] = &sizeCurvePoints_;
            dg.pts[1] = &alphaCurvePoints_;
            dg.visible[0] = sizeOn;
            dg.visible[1] = alphaOn;
            dg.Sync();
            // 凡例 + リセット
            ImGui::ColorButton("##lcS", ImVec4(1.0f, 0.6f, 0.2f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
            ImGui::SameLine();
            ImGui::TextUnformatted("サイズ");
            ImGui::SameLine();
            ImGui::ColorButton("##lcA", ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
            ImGui::SameLine();
            ImGui::TextUnformatted("アルファ");
            ImGui::SameLine();
            if (ImGui::SmallButton("リセット##lcReset"))
            {
                sizeCurvePoints_ = {{0.0f, 1.0f}, {1.0f, 1.0f}};
                alphaCurvePoints_ = {{0.0f, 1.0f}, {1.0f, 1.0f}};
                MarkLifeCurvesDirty();
            }
            ImCurveEdit::Edit(dg, ImVec2(ImGui::GetContentRegionAvail().x, 140.0f), 7321);
            if (dg.changed)
                MarkLifeCurvesDirty();
            ImGui::TextDisabled("点ドラッグ=移動 / 線上ダブルクリック=追加 / ホイール=Y拡縮");
            // プリセット
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("ポップ##lcP1"))
            {
                sizeCurvePoints_ = {{0.0f, 0.0f}, {0.2f, 1.2f}, {1.0f, 0.0f}};
                alphaCurvePoints_ = {{0.0f, 0.0f}, {0.1f, 1.0f}, {1.0f, 0.0f}};
                MarkLifeCurvesDirty();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("煙##lcP2"))
            {
                sizeCurvePoints_ = {{0.0f, 0.3f}, {1.0f, 1.0f}};
                alphaCurvePoints_ = {{0.0f, 0.0f}, {0.25f, 1.0f}, {1.0f, 0.0f}};
                MarkLifeCurvesDirty();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("フェード##lcP3"))
            {
                alphaCurvePoints_ = {{0.0f, 0.0f}, {0.15f, 1.0f}, {0.85f, 1.0f}, {1.0f, 0.0f}};
                MarkLifeCurvesDirty();
            }
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- タービュランス ----
    if (pSettingsData_->enableTurbulence &&
        EffectHeader("タービュランス（振動力）", DebugTheme::kAccentOrange,
                     [&] { pSettingsData_->enableTurbulence = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentOrange);

        bool v = pSettingsData_->enableTurbulence != 0;

        if (v)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentOrange));
            ImGui::DragFloat("振動強度##tbs", &pSettingsData_->turbulenceStrength, 0.05f, 0.0f, 50.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("大きいほど激しく揺れます\n推奨: 0.5〜5.0");
            ImGui::DragFloat("振動周波数##tbf", &pSettingsData_->turbulenceFrequency, 0.1f, 0.0f, 30.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("周波数 (Hz) — 大きいほど細かく素早く振動\n推奨: 1〜8");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("ゆらめき##tbPre1"))
            {
                pSettingsData_->turbulenceStrength = 0.8f;
                pSettingsData_->turbulenceFrequency = 2.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("嵐##tbPre2"))
            {
                pSettingsData_->turbulenceStrength = 4.0f;
                pSettingsData_->turbulenceFrequency = 6.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("細かい揺れ##tbPre3"))
            {
                pSettingsData_->turbulenceStrength = 1.5f;
                pSettingsData_->turbulenceFrequency = 10.0f;
            }
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- 音声振動 ----
    if (pSettingsData_->enableAudioVibration &&
        EffectHeader("音声振動（音の立ち上がりでバンっと揺らす）", DebugTheme::kAccentCyan,
                     [&] { pSettingsData_->enableAudioVibration = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentBlue);

        ImGui::TextDisabled("音が大きくなった“瞬間”にバンっと強く震え、その後スッと落ち着きます（各粒子バラバラ／形状を選びません）");

        ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
        ImGui::DragFloat("感度##avsens", &pSettingsData_->audioVibrationSensitivity, 0.05f, 0.0f, 50.0f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("音の立ち上がりへの反応の強さ（入力ゲイン）。大きいほど小さなビートにも反応\n推奨: 2〜10");
        ImGui::DragFloat("振動の大きさ##avs", &pSettingsData_->audioVibrationStrength, 0.1f, 0.0f, 200.0f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("揺れ幅。大きいほど激しく振動\n推奨: 6〜40");
        ImGui::DragFloat("振動の速さ##avfreq", &pSettingsData_->audioVibrationFrequency, 0.2f, 0.0f, 120.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("震える速さ（Hz的スケール）。大きいほど細かくブルブル震える\n推奨: 12〜40");
        ImGui::DragFloat("反応カーブ##avsharp", &pSettingsData_->audioAttackSharpness, 0.02f, 0.1f, 8.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("反応の鋭さ（指数）。1より大きいほど「大きい音だけドンと・小さい音は無視」\n推奨: 1.5〜3");
        ImGui::DragFloat("落ち着く速さ##avrel", &pSettingsData_->audioReleaseRate, 0.1f, 0.5f, 60.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("バンっの後どれだけ早く静まるか[1/s]。大きいほど一瞬で落ち着く（キレが増す）\n推奨: 6〜20");
        ImGui::PopStyleColor();

        // エンベロープを可視化（CB 注入値をそのまま表示。ビートで跳ねて減衰すれば駆動できている）
        ImGui::Spacing();
        ImGui::TextDisabled("立ち上がり:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.75f, 0.9f, 1.0f));
        ImGui::ProgressBar(pSettingsData_->audioAmplitude, ImVec2(-1.0f, 0.0f));
        ImGui::PopStyleColor();

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- 終了スケール ----
    if (pSettingsData_->enableEndScale &&
        EffectHeader("終了スケール", DebugTheme::kAccentCyan,
                     [&] { pSettingsData_->enableEndScale = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
        ImGui::TextDisabled("初期スケール→終了スケールへ寿命に応じてlerp（「寿命で縮小」より優先）");

        bool v = pSettingsData_->enableEndScale != 0;

        if (v)
        {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
            ImGui::DragFloat3("終了スケール##esv", &pSettingsData_->endScaleValue.x, 0.01f, 0.0f, 9999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("寿命終了時のスケール(XYZ)\n0,0,0 で消える / 初期値と同じなら変化なし");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("消える##esPreset1"))
            {
                pSettingsData_->endScaleValue = {0.0f, 0.0f, 0.0f};
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("大きくなる##esPreset2"))
            {
                pSettingsData_->endScaleValue = {
                    pSettingsData_->scaleMax * 2.0f,
                    pSettingsData_->scaleMax * 2.0f,
                    pSettingsData_->scaleMax * 2.0f};
            }
            ImGui::Unindent();
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- 回転（ランダム初期角度 / ランダム角速度）----
    if ((pSettingsData_->enableRandomRotation || pSettingsData_->enableRandomAngularVelocity) &&
        EffectHeader("回転", DebugTheme::kAccentPurple,
                     [&] { pSettingsData_->enableRandomRotation = 0; pSettingsData_->enableRandomAngularVelocity = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentPurple);

        // ---- ランダム初期角度 ----
        {
            bool v = pSettingsData_->enableRandomRotation != 0;
            if (ImGui::Checkbox("ランダム初期角度##rr", &v))
                pSettingsData_->enableRandomRotation = v ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("発生時にランダムな角度で出現します (XYZ, ラジアン)");
            if (v)
            {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentPurple));

                auto toDeg3 = [](Vector3 r) -> Vector3 { return {r.x * (180.0f / 3.14159265f), r.y * (180.0f / 3.14159265f), r.z * (180.0f / 3.14159265f)}; };
                auto toRad3 = [](Vector3 d) -> Vector3 { return {d.x * (3.14159265f / 180.0f), d.y * (3.14159265f / 180.0f), d.z * (3.14159265f / 180.0f)}; };

                Vector3 rotMinDeg = toDeg3(pSettingsData_->rotationMin);
                Vector3 rotMaxDeg = toDeg3(pSettingsData_->rotationMax);

                if (ImGui::DragFloat3("角度 Min(°)##rrMin", &rotMinDeg.x, 1.0f, -360.0f, 360.0f, "%.1f°"))
                    pSettingsData_->rotationMin = toRad3(rotMinDeg);
                if (ImGui::DragFloat3("角度 Max(°)##rrMax", &rotMaxDeg.x, 1.0f, -360.0f, 360.0f, "%.1f°"))
                    pSettingsData_->rotationMax = toRad3(rotMaxDeg);

                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::TextDisabled("プリセット:");
                ImGui::SameLine();
                if (ImGui::SmallButton("全方向##rrPreset1"))
                {
                    pSettingsData_->rotationMin = {0.0f, 0.0f, 0.0f};
                    pSettingsData_->rotationMax = {6.2831853f, 6.2831853f, 6.2831853f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Z軸のみ##rrPreset2"))
                {
                    pSettingsData_->rotationMin = {0.0f, 0.0f, 0.0f};
                    pSettingsData_->rotationMax = {0.0f, 0.0f, 6.2831853f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("リセット##rrPreset3"))
                {
                    pSettingsData_->rotationMin = {0.0f, 0.0f, 0.0f};
                    pSettingsData_->rotationMax = {0.0f, 0.0f, 0.0f};
                }
                ImGui::Unindent();
            }
        }

        ImGui::Spacing();

        // ---- ランダム角速度 ----
        {
            bool v = pSettingsData_->enableRandomAngularVelocity != 0;
            if (ImGui::Checkbox("ランダム角速度##rav", &v))
                pSettingsData_->enableRandomAngularVelocity = v ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("発生時にランダムな回転速度を設定します (XYZ, ラジアン/秒)");
            if (v)
            {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentPurple));

                auto toDeg3 = [](Vector3 r) -> Vector3 { return {r.x * (180.0f / 3.14159265f), r.y * (180.0f / 3.14159265f), r.z * (180.0f / 3.14159265f)}; };
                auto toRad3 = [](Vector3 d) -> Vector3 { return {d.x * (3.14159265f / 180.0f), d.y * (3.14159265f / 180.0f), d.z * (3.14159265f / 180.0f)}; };

                Vector3 avMinDeg = toDeg3(pSettingsData_->angularVelocityMin);
                Vector3 avMaxDeg = toDeg3(pSettingsData_->angularVelocityMax);

                if (ImGui::DragFloat3("角速度 Min(°/s)##ravMin", &avMinDeg.x, 1.0f, -3600.0f, 3600.0f, "%.1f°/s"))
                    pSettingsData_->angularVelocityMin = toRad3(avMinDeg);
                if (ImGui::DragFloat3("角速度 Max(°/s)##ravMax", &avMaxDeg.x, 1.0f, -3600.0f, 3600.0f, "%.1f°/s"))
                    pSettingsData_->angularVelocityMax = toRad3(avMaxDeg);

                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::TextDisabled("プリセット:");
                ImGui::SameLine();
                if (ImGui::SmallButton("ゆっくり##ravPreset1"))
                {
                    pSettingsData_->angularVelocityMin = {-1.0f, -1.0f, -1.0f};
                    pSettingsData_->angularVelocityMax = {1.0f, 1.0f, 1.0f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("速い##ravPreset2"))
                {
                    pSettingsData_->angularVelocityMin = {-6.2831853f, -6.2831853f, -6.2831853f};
                    pSettingsData_->angularVelocityMax = {6.2831853f, 6.2831853f, 6.2831853f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Z軸のみ##ravPreset3"))
                {
                    pSettingsData_->angularVelocityMin = {0.0f, 0.0f, -3.14159265f};
                    pSettingsData_->angularVelocityMax = {0.0f, 0.0f, 3.14159265f};
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("リセット##ravPreset4"))
                {
                    pSettingsData_->angularVelocityMin = {0.0f, 0.0f, 0.0f};
                    pSettingsData_->angularVelocityMax = {0.0f, 0.0f, 0.0f};
                }
                ImGui::Unindent();
            }
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // ---- 放射状速度 ----
    if (pSettingsData_->enableRadialVelocity &&
        EffectHeader("放射状速度", DebugTheme::kAccentOrange,
                     [&] { pSettingsData_->enableRadialVelocity = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentOrange);
        ImGui::TextDisabled("中心点から放射状に飛び散る速度（花火・爆発の演出に）");

        bool v = pSettingsData_->enableRadialVelocity != 0;

        if (v)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentOrange));
            ImGui::DragFloat("放射強度##rs", &pSettingsData_->radialVelocityStrength, 0.1f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat("ランダム性##rr", &pSettingsData_->radialVelocityRandomness, 0.01f, 0.0f, 1.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0=完全放射状 / 1=完全ランダム\n推奨: 0.1-0.3");
            ImGui::DragFloat3("放射中心##rc", &pSettingsData_->radialVelocityCenter.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("花火"))
            {
                pSettingsData_->enableRadialVelocity = 1;
                pSettingsData_->radialVelocityStrength = 5.0f;
                pSettingsData_->radialVelocityRandomness = 0.2f;
                pSettingsData_->enableGravity = 1;
                pSettingsData_->gravity = {0, -9.8f, 0};
                pSettingsData_->enableVelocityDamping = 1;
                pSettingsData_->velocityDampingFactor = 0.95f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("爆発"))
            {
                pSettingsData_->enableRadialVelocity = 1;
                pSettingsData_->radialVelocityStrength = 8.0f;
                pSettingsData_->radialVelocityRandomness = 0.3f;
                pSettingsData_->enableGravity = 1;
                pSettingsData_->gravity = {0, -9.8f, 0};
                pSettingsData_->enableLifetimeVelocityDamping = 1;
                pSettingsData_->lifetimeVelocityDampingStart = 0.7f;
            }
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

}

#endif // USE_IMGUI
} // namespace Hagine
