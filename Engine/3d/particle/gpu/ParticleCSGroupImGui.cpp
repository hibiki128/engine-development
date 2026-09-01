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
#endif

// ParticleCSGroup のエディタUI（DrawImGui とその補助）だけを集めたファイル。
// GPU リソースの生成・ディスパッチ本体は ParticleCSGroup.cpp にある。
namespace Hagine {
#ifdef USE_IMGUI
namespace {
// ImGradient ウィジェット用デリゲート。GradientStop 列(RGBA+位置)を
// ImVec4(xyz=RGB, w=位置) のスクラッチ配列を介して編集する（2Dエンジンの ColorGradient と同型）。
struct ColorGradientDelegate : public ImGradient::Delegate
{
    std::vector<Hagine::GradientStop> *stops = nullptr;
    std::vector<ImVec4> scratch;
    std::vector<Hagine::GradientStop> sorted;
    void Sync()
    {
        if (!stops)
        {
            scratch.clear();
            sorted.clear();
            return;
        }
        scratch.resize(stops->size());
        for (size_t i = 0; i < stops->size(); ++i)
        {
            const auto &s = (*stops)[i];
            scratch[i] = ImVec4(s.color.x, s.color.y, s.color.z, s.pos);
        }
        sorted = *stops;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Hagine::GradientStop &a, const Hagine::GradientStop &b) { return a.pos < b.pos; });
    }
    Hagine::Vector4 Sample(float t) const
    {
        if (sorted.empty())
            return {1.0f, 1.0f, 1.0f, 1.0f};
        if (t <= sorted.front().pos)
            return sorted.front().color;
        if (t >= sorted.back().pos)
            return sorted.back().color;
        for (size_t i = 1; i < sorted.size(); ++i)
        {
            if (t <= sorted[i].pos)
            {
                const auto &a = sorted[i - 1].color;
                const auto &b = sorted[i].color;
                float span = sorted[i].pos - sorted[i - 1].pos;
                float u = span > 1e-6f ? (t - sorted[i - 1].pos) / span : 0.0f;
                return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
                        a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u};
            }
        }
        return sorted.back().color;
    }
    size_t GetPointCount() override { return stops ? stops->size() : 0; }
    ImVec4 *GetPoints() override { return scratch.data(); }
    int EditPoint(int index, ImVec4 value) override
    {
        if (!stops || index < 0 || index >= static_cast<int>(stops->size()))
            return index;
        float p = value.w;
        p = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
        (*stops)[index].pos = p;
        (*stops)[index].color.x = value.x;
        (*stops)[index].color.y = value.y;
        (*stops)[index].color.z = value.z;
        if (index < static_cast<int>(scratch.size()))
            scratch[index] = ImVec4(value.x, value.y, value.z, p);
        return index;
    }
    ImVec4 GetPoint(float t) override
    {
        Hagine::Vector4 c = Sample(t);
        return ImVec4(c.x, c.y, c.z, t);
    }
    void AddPoint(ImVec4 value) override
    {
        if (!stops)
            return;
        Hagine::GradientStop s;
        s.pos = value.w;
        Hagine::Vector4 sampled = Sample(value.w); // アルファは既存グラデから補間して引き継ぐ
        s.color = {value.x, value.y, value.z, sampled.w};
        stops->push_back(s);
        Sync();
    }
};

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
#endif

void ParticleCSGroup::DrawImGui()
{
#ifdef USE_IMGUI
    if (!pSettingsData_)
        return;

    auto PushSectionColor = [](ImVec4 col) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(col.x * 0.45f, col.y * 0.45f, col.z * 0.45f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(col.x * 0.55f, col.y * 0.55f, col.z * 0.55f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(col.x * 0.65f, col.y * 0.65f, col.z * 0.65f, 0.85f));
    };
    auto PopSectionColor = []() { ImGui::PopStyleColor(3); };

    ImGui::PushItemWidth(-120.0f);

    // エフェクトを「コア（常設）＋ 追加したものだけのカード」で構成する。
    // 各エフェクトカードのヘッダ（× 削除ボタン付き）。展開中かどうかを返す。
    // onRemove で enable フラグを 0 にすると、そのエフェクトは非表示になり「＋追加」リストへ戻る。
    auto effectHeader = [&](const char *label, ImVec4 col, const std::function<void()> &onRemove) -> bool {
        ImGui::PushID(label);
        PushSectionColor(col);
        // AllowOverlap: 後続の × ボタンをヘッダに重ねてもクリックがボタン側に渡るようにする
        // （これが無いとヘッダが全幅でクリックを奪い、× が押せず開閉だけになる）。
        bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        PopSectionColor();
        // ヘッダ右端に × 削除ボタン（ヘッダに重ねて配置）
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 28.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.2f, 0.2f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.3f, 0.3f, 1.0f));
        if (ImGui::SmallButton("✕"))
            onRemove();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("このエフェクトを削除");
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        return open;
    };

    // 「演出の基準空間」セレクタ。渦の回転軸と、渦/集束の目標オフセットの解釈をまとめて切り替える。
    // 同じ pSettingsData_->effectSpace を指すので、ギャザー側で変えても渦側に反映される。
    auto effectSpaceCombo = [&](const char *id) {
        static const char *kNames[] = {"ワールド固定", "エミッター基準", "ビルボード（カメラ）"};
        int space = static_cast<int>(pSettingsData_->effectSpace);
        if (space < 0 || space > 2)
            space = 0;
        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentBlue));
        if (ImGui::Combo("基準空間", &space, kNames, IM_ARRAYSIZE(kNames)))
            pSettingsData_->effectSpace = static_cast<uint32_t>(space);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("回転軸と目標座標をどの空間の値として扱うか（ギャザーと渦で共通）。\n\n"
                              "ワールド固定     : 従来動作。エミッターやカメラを回しても軸は動かない。\n"
                              "エミッター基準   : エミッターの回転に軸も追従する。\n"
                              "                   エミッター側の「ビルボード」と併用すると\n"
                              "                   発生形状ごとカメラへ正対する。\n"
                              "ビルボード（カメラ）: 軸がカメラの向きに追従する。\n"
                              "                   Z=(0,0,1) なら常に画面と平行に渦が回る。");
        ImGui::PopID();
    };

    // ---- GPU駆動の視錐台カリング（常設・既定ON）----
    // 画面に映らない粒子を描画リストから外し、ExecuteIndirect の instanceCount を減らす。
    // シミュレーションは続くので、切り替えても粒子の動きは変わらない（見えるかどうかだけ）。
    {
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
        bool cull = frustumCullEnabled_;
        if (ImGui::Checkbox("視錐台カリング(GPU)", &cull))
            frustumCullEnabled_ = cull;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("画面外の粒子を描画リストから外します（頂点シェーダも起動しません）。\n"
                              "シミュレーションは続くので動きは変わりません。\n"
                              "※ プレビュー窓は別カメラなので自動的に無効化されます。");
    }
    ImGui::Spacing();

    // =======================================================
    // 1. 出現・寿命・サイズ（赤系）【コア・常設】
    // =======================================================
    PushSectionColor(ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
    bool openBasic = ImGui::CollapsingHeader("  出現 / 寿命 / サイズ");
    PopSectionColor();
    if (openBasic)
    {
        ImGui::Indent();

        // 出現数
        {
            int emitCount = static_cast<int>(pSettingsData_->emitCount);
            int dynMax = CalculateOptimalEmitCount();
            int absMax = static_cast<int>(pSettingsData_->maxParticleCount);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentRed));
            if (ImGui::DragInt("出現数", &emitCount, 1, 0, 100000))
                pSettingsData_->emitCount = static_cast<uint32_t>(emitCount);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("推奨上限: %d  /  絶対上限: %d", dynMax, absMax);
            if (emitCount > dynMax)
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
                ImGui::TextUnformatted(" 推奨超過");
                ImGui::PopStyleColor();
            }
        }

        // 寿命（横並び）
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentRed));
            float hw = (ImGui::GetContentRegionAvail().x - 130.0f) * 0.5f - 4.0f;
            ImGui::SetNextItemWidth(hw);
            ImGui::DragFloat("##lifeMin", &pSettingsData_->lifeTimeMin, 0.1f, 0.0f, 9999.0f, "Min %.4fs");
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(hw);
            ImGui::DragFloat("##lifeMax", &pSettingsData_->lifeTimeMax, 0.1f, 0.0f, 9999.0f, "Max %.4fs");
            ImGui::SameLine();
            ImGui::TextUnformatted("寿命(s)");
            ImGui::PopStyleColor();
        }

        // サイズ（横並び）
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentYellow));
            float hw = (ImGui::GetContentRegionAvail().x - 130.0f) * 0.5f - 4.0f;
            ImGui::SetNextItemWidth(hw);
            ImGui::DragFloat("##scMin", &pSettingsData_->scaleMin, 0.01f, 0.0f, 9999.0f, "Min %.4f");
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(hw);
            ImGui::DragFloat("##scMax", &pSettingsData_->scaleMax, 0.01f, 0.0f, 9999.0f, "Max %.4f");
            ImGui::SameLine();
            ImGui::TextUnformatted("サイズ");
            ImGui::PopStyleColor();
        }

        ImGui::Unindent();
    }

    // =======================================================
    // 2. 速度・色彩・ブレンド（青系）
    // =======================================================
    PushSectionColor(ImVec4(0.25f, 0.45f, 0.8f, 1.0f));
    bool openAppearance = ImGui::CollapsingHeader("  速度 / 色彩 / ブレンド");
    PopSectionColor();
    if (openAppearance)
    {
        ImGui::Indent();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentBlue));
        ImGui::DragFloat3("速度 Min", &pSettingsData_->velocityMin.x, 0.01f, -9999.0f, 9999.0f, "%.4f");
        ImGui::DragFloat3("速度 Max", &pSettingsData_->velocityMax.x, 0.01f, -9999.0f, 9999.0f, "%.4f");
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // 色彩
        {
            // グラデーション(多段) モード — ON で寿命に沿った N段カラーを使う（既存の3段/ランダムを上書き）
            bool grad = pSettingsData_->enableColorGradient != 0;
            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentOrange);
            if (ImGui::Checkbox("グラデーション(多段)", &grad))
            {
                pSettingsData_->enableColorGradient = grad ? 1 : 0;
                MarkColorStopsDirty();
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("寿命に沿った多段カラーグラデーション\nバー上ダブルクリックで色追加 / 点ドラッグで移動 / 選択して色・削除");

            if (grad)
            {
                // ===== ImGradient エディタ（連続プレビューバー + ストップ編集） =====
                ColorGradientDelegate dg;
                dg.stops = &colorStops_;
                dg.Sync();
                // 連続グラデーションのプレビューバー（RGBのみ。アルファはフェードとして別途効く）
                {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    float barW = ImGui::GetContentRegionAvail().x;
                    const float barH = 16.0f;
                    const int kSteps = 64;
                    for (int i = 0; i < kSteps; ++i)
                    {
                        float t0 = static_cast<float>(i) / kSteps;
                        float t1 = static_cast<float>(i + 1) / kSteps;
                        Vector4 c0 = dg.Sample(t0);
                        Vector4 c1 = dg.Sample(t1);
                        ImU32 u0 = ImGui::ColorConvertFloat4ToU32(ImVec4(c0.x, c0.y, c0.z, 1.0f));
                        ImU32 u1 = ImGui::ColorConvertFloat4ToU32(ImVec4(c1.x, c1.y, c1.z, 1.0f));
                        dl->AddRectFilledMultiColor(ImVec2(p0.x + barW * t0, p0.y),
                                                    ImVec2(p0.x + barW * t1, p0.y + barH), u0, u1, u1, u0);
                    }
                    ImGui::Dummy(ImVec2(barW, barH));
                }
                int sel = -1;
                if (ImGradient::Edit(dg, ImVec2(ImGui::GetContentRegionAvail().x, 40.0f), sel))
                    MarkColorStopsDirty();
                ImGui::TextDisabled("点ドラッグ=移動 / バー上ダブルクリック=追加");
                if (sel >= 0 && sel < static_cast<int>(colorStops_.size()))
                {
                    if (ImGui::ColorEdit4("ストップ RGBA##grad", &colorStops_[sel].color.x))
                        MarkColorStopsDirty();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("削除##gradStop") && colorStops_.size() > 1)
                    {
                        colorStops_.erase(colorStops_.begin() + sel);
                        MarkColorStopsDirty();
                    }
                }
                else
                {
                    ImGui::TextDisabled("(ストップ未選択 — バー上の点をクリックで選択)");
                }
                // プリセット
                ImGui::TextDisabled("プリセット:");
                ImGui::SameLine();
                if (ImGui::SmallButton("炎##gradPre1"))
                {
                    colorStops_ = {{{1.0f, 1.0f, 0.6f, 1.0f}, 0.0f}, {{1.0f, 0.55f, 0.1f, 1.0f}, 0.35f}, {{0.9f, 0.12f, 0.0f, 0.6f}, 0.75f}, {{0.3f, 0.0f, 0.0f, 0.0f}, 1.0f}};
                    MarkColorStopsDirty();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("虹##gradPre2"))
                {
                    colorStops_ = {{{1.0f, 0.3f, 0.4f, 1.0f}, 0.0f}, {{0.3f, 0.8f, 1.0f, 1.0f}, 0.33f}, {{1.0f, 0.9f, 0.3f, 1.0f}, 0.66f}, {{0.5f, 1.0f, 0.5f, 0.0f}, 1.0f}};
                    MarkColorStopsDirty();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("魔法##gradPre3"))
                {
                    colorStops_ = {{{0.8f, 0.4f, 1.0f, 1.0f}, 0.0f}, {{0.4f, 0.7f, 1.0f, 1.0f}, 0.45f}, {{1.0f, 0.9f, 1.0f, 0.7f}, 0.8f}, {{0.6f, 0.4f, 1.0f, 0.0f}, 1.0f}};
                    MarkColorStopsDirty();
                }
            }
            else
            {
                bool rnd = pSettingsData_->enableRandomColor != 0;
                ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentOrange);
                if (ImGui::Checkbox("ランダムカラー", &rnd))
                    pSettingsData_->enableRandomColor = rnd ? 1 : 0;
                ImGui::PopStyleColor();
                if (!rnd)
                {
                    ImGui::ColorEdit4("開始色", &pSettingsData_->startColor.x);
                    // 中間色
                    {
                        bool mc = pSettingsData_->enableMidColor != 0;
                        if (ImGui::Checkbox("中間色を有効化##mc", &mc))
                            pSettingsData_->enableMidColor = mc ? 1 : 0;
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("開始→中間→終了の3段階カラーグラデーション");
                        if (mc)
                        {
                            ImGui::Indent();
                            ImGui::ColorEdit4("中間色##mcc", &pSettingsData_->midColor.x);
                            ImGui::DragFloat("中間タイミング##mcr", &pSettingsData_->midColorRatio, 0.01f, 0.0f, 1.0f, "%.2f");
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("中間色に達するlife比率\n0=開始直後 / 0.5=寿命半分 / 1=終了直前");
                            ImGui::Spacing();
                            ImGui::TextDisabled("プリセット:");
                            ImGui::SameLine();
                            if (ImGui::SmallButton("炎##mcPre1"))
                            {
                                pSettingsData_->startColor = {1.0f, 0.3f, 0.0f, 1.0f};
                                pSettingsData_->midColor = {1.0f, 1.0f, 0.3f, 1.0f};
                                pSettingsData_->endColor = {0.2f, 0.2f, 0.2f, 0.0f};
                                pSettingsData_->midColorRatio = 0.35f;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("魔法陣##mcPre2"))
                            {
                                pSettingsData_->startColor = {0.2f, 0.5f, 1.0f, 0.0f};
                                pSettingsData_->midColor = {1.0f, 1.0f, 1.0f, 1.0f};
                                pSettingsData_->endColor = {0.5f, 0.2f, 1.0f, 0.0f};
                                pSettingsData_->midColorRatio = 0.5f;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("雷##mcPre3"))
                            {
                                pSettingsData_->startColor = {1.0f, 1.0f, 1.0f, 1.0f};
                                pSettingsData_->midColor = {0.7f, 0.9f, 1.0f, 0.8f};
                                pSettingsData_->endColor = {0.2f, 0.4f, 0.8f, 0.0f};
                                pSettingsData_->midColorRatio = 0.4f;
                            }
                            ImGui::Unindent();
                        }
                    }
                    ImGui::ColorEdit4("終了色", &pSettingsData_->endColor.x);
                }
                else
                {
                    // ランダムカラー時はRGBがランダムのため色編集は非表示にするが、
                    // アルファ（透明度）は startColor.a / endColor.a で補間されるので個別に編集できるようにする
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentBlue));
                    ImGui::DragFloat("開始アルファ##rndAlphaStart", &pSettingsData_->startColor.w, 0.01f, 0.0f, 1.0f, "%.4f");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("発生時の透明度 (0=完全透明, 1=完全不透明)");
                    ImGui::DragFloat("終了アルファ##rndAlphaEnd", &pSettingsData_->endColor.w, 0.01f, 0.0f, 1.0f, "%.4f");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("消滅時の透明度 (0=完全透明, 1=完全不透明)");
                    ImGui::PopStyleColor();
                }
            } // else: グラデーション(多段) OFF
        }

        ImGui::Spacing();

        // ブレンドモード
        {
            const char *blendNames[] = {"なし", "通常", "加算", "減算", "乗算", "スクリーン"};
            int bm = static_cast<int>(particleGroupData_.blendMode);
            if (ImGui::Combo("ブレンドモード", &bm, blendNames, IM_ARRAYSIZE(blendNames)))
                particleGroupData_.blendMode = static_cast<BlendMode>(bm);
        }

        ImGui::Unindent();
    }

    // =======================================================
    // 2.5 テクスチャ（画像差し替え・水色系）
    // =======================================================
    PushSectionColor(ImVec4(0.4f, 0.7f, 0.75f, 1.0f));
    bool openTex = ImGui::CollapsingHeader("  テクスチャ");
    PopSectionColor();
    if (openTex && !particleGroupData_.materials.empty())
    {
        ImGui::Indent();
        // images ルート配下の画像を列挙（初回スキャン + 再スキャンボタン）。
        // textureFilePath は base からの相対パス('/'区切り)で持つ規約に合わせる。
        static std::vector<std::string> s_imageFiles;
        static bool s_scanned = false;
        auto scanImages = []() {
            s_imageFiles.clear();
            std::error_code ec;
            // images はエンジン(debug)とアプリの 2 ルートに分割されているため両方を走査する。
            for (const std::string &base : AssetPath::ImageScanRoots())
            {
                if (!std::filesystem::exists(base, ec))
                    continue;
                for (auto &e : std::filesystem::recursive_directory_iterator(base, ec))
                {
                    if (ec)
                        break;
                    if (!e.is_regular_file())
                        continue;
                    std::string ext = e.path().extension().string();
                    for (auto &ch : ext)
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".dds")
                        continue;
                    std::string rel = std::filesystem::relative(e.path(), base, ec).generic_string();
                    if (!rel.empty())
                        s_imageFiles.push_back(rel);
                }
            }
            std::sort(s_imageFiles.begin(), s_imageFiles.end());
        };
        if (!s_scanned)
        {
            scanImages();
            s_scanned = true;
        }

        std::string &curPath = particleGroupData_.materials[0].textureFilePath;

        // テクスチャを差し替えて全マテリアルへ反映するヘルパー。
        // 描画は毎フレーム textureFilePath で引くので、パス差し替え + LoadTexture で即時反映される。
        auto applyTexture = [&](const std::string &path) {
            SetTexture(path);
        };

        // 選択中テクスチャのサムネイルプレビュー（読み込み済み前提）。
        // ※ GetSrvHandleGPU は他の getter と違い相対パスを前置しない＝フルパス(＝マップキー)を要求する。
        if (!curPath.empty())
        {
            pTextureManager_->LoadTexture(curPath); // 念のため未ロードならロード（ロード済みなら即return）
            // キューブマップは SRV が TEXTURECUBE。Texture2D として Image 描画すると
            // GPU ベース検証 #940 で落ちるためプレビューしない。
            if (pTextureManager_->GetMetaData(curPath).IsCubemap())
            {
                ImGui::Button("CUBE", ImVec2(56.0f, 56.0f));
            }
            else
            {
                D3D12_GPU_DESCRIPTOR_HANDLE h = pTextureManager_->GetSrvHandleGPU(AssetPath::Image(curPath));
                if (h.ptr != 0)
                    ImGui::Image(static_cast<ImTextureID>(h.ptr), ImVec2(56.0f, 56.0f));
                else
                    ImGui::Button("画像\nなし", ImVec2(56.0f, 56.0f));
            }
        }
        else
        {
            // 未設定。アセットブラウザからのドロップ先となるプレースホルダ。
            ImGui::Button("ここへ\nドロップ", ImVec2(56.0f, 56.0f));
        }
        // サムネ（またはプレースホルダ）をアセットブラウザからのドロップ先にする。
        {
            std::string dropped;
            if (AssetDragDrop::TextureTarget(dropped))
                applyTexture(dropped);
        }
        ImGui::SameLine();

        ImGui::BeginGroup();
        if (ImGui::BeginCombo("画像", curPath.c_str()))
        {
            for (const std::string &f : s_imageFiles)
            {
                bool sel = (f == curPath);
                if (ImGui::Selectable(f.c_str(), sel))
                    applyTexture(f); // パスを差し替え（毎フレーム path 参照なので即時反映）
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        // コンボもドロップ先にする。
        {
            std::string dropped;
            if (AssetDragDrop::TextureTarget(dropped))
                applyTexture(dropped);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("再スキャン"))
            scanImages();
        ImGui::TextDisabled("画像から選択 / アセットブラウザからのD&Dでも設定可");
        ImGui::EndGroup();
        ImGui::Unindent();
    }

    // ビルボード（コア・常設）
    ImGui::Spacing();
    {
        bool v = pPerViewData_->enableBillboard != 0;
        if (ImGui::Checkbox("ビルボード（常にカメラを向く）", &v))
            pPerViewData_->enableBillboard = v ? 1 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ONでパーティクルが常にカメラ正面を向きます\nOFFにするとワールド空間に固定されます");
    }

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

    const ImVec4 kMotionColor = ImVec4(0.5f, 0.75f, 0.2f, 1.0f);

    // ---- 寿命で縮小 ----
    if (pSettingsData_->enableLifetimeScale)
    {
        if (effectHeader("寿命で縮小", kMotionColor, [&] { pSettingsData_->enableLifetimeScale = 0; }))
        {
            ImGui::Indent();
            ImGui::TextDisabled("時間経過と共にスケールが 0 に近づきます（パラメータなし）");
            ImGui::Unindent();
        }
    }

    // ---- Sin波で拡縮 ----
    if (pSettingsData_->enableSinScale)
    {
        if (effectHeader("Sin波で拡縮", kMotionColor, [&] { pSettingsData_->enableSinScale = 0; }))
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
        if (effectHeader("重力", kMotionColor, [&] { pSettingsData_->enableGravity = 0; }))
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
        if (effectHeader("加速度", kMotionColor, [&] { pSettingsData_->enableAcceleration = 0; }))
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
        if (effectHeader("速度減衰", kMotionColor, [&] { pSettingsData_->enableVelocityDamping = 0; }))
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
        if (effectHeader("寿命による速度減衰", kMotionColor, [&] { pSettingsData_->enableLifetimeVelocityDamping = 0; }))
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
        if (effectHeader("速度ストレッチ", kMotionColor, [&] { pPerViewData_->enableVelocityStretch = 0; }))
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
        effectHeader("描画カリング（overdraw対策）", ImVec4(0.3f, 0.7f, 0.8f, 1.0f),
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
        effectHeader("寿命カーブ（サイズ/アルファ）", ImVec4(0.6f, 0.45f, 0.8f, 1.0f),
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
        effectHeader("タービュランス（振動力）", ImVec4(0.9f, 0.55f, 0.1f, 1.0f),
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
        effectHeader("音声振動（音の立ち上がりでバンっと揺らす）", ImVec4(0.35f, 0.75f, 0.9f, 1.0f),
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
        effectHeader("終了スケール", ImVec4(0.2f, 0.7f, 0.65f, 1.0f),
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
        effectHeader("回転", ImVec4(0.75f, 0.3f, 0.75f, 1.0f),
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
        effectHeader("放射状速度", ImVec4(0.85f, 0.5f, 0.1f, 1.0f),
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

    // =======================================================
    // 発生形状（ピンク系）【コア・常設】
    // =======================================================
    PushSectionColor(ImVec4(0.85f, 0.35f, 0.6f, 1.0f));
    bool openEmitShape = ImGui::CollapsingHeader("  発生形状");
    PopSectionColor();
    if (openEmitShape)
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentRed);

        const char *shapeNames[] = {"Box（直方体）", "Sphere Surface（球面）", "Cone（コーン）"};
        int shape = static_cast<int>(pSettingsData_->emitShape);
        if (ImGui::Combo("形状##es", &shape, shapeNames, IM_ARRAYSIZE(shapeNames)))
            pSettingsData_->emitShape = static_cast<uint32_t>(shape);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "モデル/プリミティブ付きエミッターには適用されません（モデルなしのみ有効）\n"
                "Box: エミッターのScaleが発生ボックスの半辺になります\n"
                "Sphere/Cone: 下の半径パラメータで範囲を指定");

        if (pSettingsData_->emitShape == 0)
        {
            ImGui::TextDisabled("  ← エミッターのScale（変換設定）で発生範囲を調整");
        }

        if (pSettingsData_->emitShape == 1 || pSettingsData_->emitShape == 2)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentRed));
            ImGui::DragFloat("半径##esr", &pSettingsData_->emitSphereRadius, 0.05f, 0.0f, 999.0f, "%.4f");
            if (pSettingsData_->emitShape == 2)
            {
                float angleDeg = pSettingsData_->emitConeAngle * (180.0f / 3.14159265f);
                if (ImGui::DragFloat("半開角(°)##eca", &angleDeg, 1.0f, 1.0f, 180.0f, "%.1f°"))
                    pSettingsData_->emitConeAngle = angleDeg * (3.14159265f / 180.0f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("コーンの広がり角度（片側）\n30°=細め / 90°=半球");
            }
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (pSettingsData_->emitShape == 1)
            {
                if (ImGui::SmallButton("小球##espre1"))
                    pSettingsData_->emitSphereRadius = 0.5f;
                ImGui::SameLine();
                if (ImGui::SmallButton("爆発球##espre2"))
                    pSettingsData_->emitSphereRadius = 2.0f;
            }
            else
            {
                if (ImGui::SmallButton("細コーン##ecpre1"))
                {
                    pSettingsData_->emitSphereRadius = 3.0f;
                    pSettingsData_->emitConeAngle = 0.2618f; // 15°
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("広コーン##ecpre2"))
                {
                    pSettingsData_->emitSphereRadius = 2.0f;
                    pSettingsData_->emitConeAngle = 0.7854f; // 45°
                }
            }
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- ギャザー（集合）----
    if (pSettingsData_->enableGather &&
        effectHeader("ギャザー（集合）", ImVec4(0.6f, 0.2f, 0.8f, 1.0f),
                     [&] { pSettingsData_->enableGather = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentPurple);

        bool v = pSettingsData_->enableGather != 0;

        if (v)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentPurple));
            ImGui::DragFloat("開始タイミング##gs", &pSettingsData_->gatherStartRatio, 0.01f, 0.0f, 1.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("寿命の何%%から引き寄せを開始するか");
            ImGui::DragFloat("ギャザー強度##gstr", &pSettingsData_->gatherStrength, 0.1f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat3("目標座標##gt", &pSettingsData_->gatherTargetOffset.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();
            effectSpaceCombo("gatherSpace");
            ImGui::TextDisabled("  解決後: %.2f, %.2f, %.2f",
                                pSettingsData_->gatherTarget.x, pSettingsData_->gatherTarget.y, pSettingsData_->gatherTarget.z);
            bool gft = pSettingsData_->enableGatherForTrail != 0;
            if (ImGui::Checkbox("トレイルにも適用##gft", &gft))
                pSettingsData_->enableGatherForTrail = gft ? 1 : 0;
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- 渦巻き（Vortex）----
    if (pSettingsData_->enableVortex &&
        effectHeader("渦巻き（Vortex）", ImVec4(0.1f, 0.65f, 0.75f, 1.0f),
                     [&] { pSettingsData_->enableVortex = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);

        bool v = pSettingsData_->enableVortex != 0;

        if (v)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
            ImGui::DragFloat("回転強度##vstr", &pSettingsData_->vortexStrength, 0.1f, -999.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("+ で正回転 / - で逆回転");
            ImGui::DragFloat3("目標座標##vt", &pSettingsData_->vortexTargetOffset.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            ImGui::PopStyleColor();

            ImGui::Text("回転軸:");
            ImGui::SameLine();
            if (ImGui::SmallButton("X##vx"))
                pSettingsData_->vortexAxisBase = {1, 0, 0};
            ImGui::SameLine();
            if (ImGui::SmallButton("Y##vy"))
                pSettingsData_->vortexAxisBase = {0, 1, 0};
            ImGui::SameLine();
            if (ImGui::SmallButton("Z##vz"))
                pSettingsData_->vortexAxisBase = {0, 0, 1};
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
            ImGui::DragFloat3("軸ベクトル##vax", &pSettingsData_->vortexAxisBase.x, 0.05f, -1.0f, 1.0f, "%.4f");
            ImGui::PopStyleColor();

            effectSpaceCombo("vortexSpace");
            if (pSettingsData_->effectSpace != 0)
            {
                // 実際に GPU へ渡っているワールド軸。カメラ/エミッターを回すとここが動く。
                ImGui::TextDisabled("  解決後の軸: %.2f, %.2f, %.2f",
                                    pSettingsData_->vortexAxis.x, pSettingsData_->vortexAxis.y, pSettingsData_->vortexAxis.z);
            }

            bool vft = pSettingsData_->enableVortexForTrail != 0;
            if (ImGui::Checkbox("トレイルにも適用##vft", &vft))
                pSettingsData_->enableVortexForTrail = vft ? 1 : 0;
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- カールノイズ ----
    if (pSettingsData_->enableCurlNoise &&
        effectHeader("カールノイズ", ImVec4(0.0f, 0.8f, 0.7f, 1.0f),
                     [&] { pSettingsData_->enableCurlNoise = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);

        bool v = pSettingsData_->enableCurlNoise != 0;

        if (v)
        {
            // --------------------------------------------------
            // ブレンドモード
            // --------------------------------------------------
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("速度合成モード");
            ImGui::PopStyleColor();
            ImGui::Separator();

            {
                int blendMode = static_cast<int>(pSettingsData_->curlNoiseBlendMode);

                // ラジオボタン：置き換え
                ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
                if (ImGui::RadioButton("置き換え##cnbm0", blendMode == 0))
                    pSettingsData_->curlNoiseBlendMode = 0;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("velocity を完全に置き換える（従来動作）\n流体的な挙動。Gather/Vortex の影響は受けない。");

                ImGui::SameLine();

                // ラジオボタン：加算
                if (ImGui::RadioButton("加算##cnbm1", blendMode == 1))
                    pSettingsData_->curlNoiseBlendMode = 1;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("既存の velocity に加算する\nGather / Vortex 等と組み合わせて使える。\n加速しすぎる場合は強度を下げるか速度減衰を併用。");
                ImGui::PopStyleColor();

                // 加算モード時の注意表示
                if (blendMode == 1)
                {
                    ImGui::Indent();
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
                    ImGui::TextUnformatted("! 速度減衰との併用を推奨");
                    ImGui::PopStyleColor();
                    ImGui::Unindent();
                }
            }

            ImGui::Spacing();

            // --------------------------------------------------
            // ノイズパラメータ
            // --------------------------------------------------
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("ノイズパラメータ");
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
            ImGui::DragFloat("スケール##cns", &pSettingsData_->curlNoiseScale, 0.01f, 0.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("小 → 大きくゆったりした渦\n大 → 細かい乱流");
            ImGui::DragFloat("強度##cnstr", &pSettingsData_->curlNoiseStrength, 0.1f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat("時間変化##cntm", &pSettingsData_->curlNoiseTimeScale, 0.01f, 0.0f, 99.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0.0 = 固定フロー / 大 = 激しく変化");
            ImGui::PopStyleColor();

            int oct = static_cast<int>(pSettingsData_->curlNoiseOctaves);
            if (ImGui::DragInt("オクターブ##cno", &oct, 1, 1, 16))
                pSettingsData_->curlNoiseOctaves = static_cast<uint32_t>(oct);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("1=軽量なめらか / 4=複雑（負荷増）");

            // --------------------------------------------------
            // 分散オフセット
            // --------------------------------------------------
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("分散オフセット");
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
            ImGui::DragFloat("分散強度##cnprs", &pSettingsData_->curlNoisePosRandomStrength, 0.05f, 0.0f, 10.0f, "%.4f");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "エミッタが小さく全員が同じ位置から生まれるとき、\n"
                    "全パーティクルが同一方向に動くのを防ぐ。\n"
                    "各パーティクル固有のオフセットをサンプリング座標に加算し\n"
                    "異なるノイズフィールドを参照させる。\n\n"
                    "0.0 = オフセットなし（従来動作）\n"
                    "推奨: 0.5〜2.0  一点から広がる演出に");

            // 分散強度が有効なとき視覚的な補足を表示
            if (pSettingsData_->curlNoisePosRandomStrength > 0.0f)
            {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 0.8f));
                ImGui::Text("  現在: %.4f  (各パーティクルが固有の方向に発散)", pSettingsData_->curlNoisePosRandomStrength);
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }

            // --------------------------------------------------
            // 引き戻し設定
            // --------------------------------------------------
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("引き戻し設定");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
            ImGui::DragFloat("引き戻し強度##cna", &pSettingsData_->curlNoiseAttractStrength, 0.01f, 0.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = 無効\n大きいほどエミッター付近に密集して流れる");
            ImGui::DragFloat3("引き戻しオフセット##cnac", &pSettingsData_->curlNoiseAttractCenter.x, 0.1f, -9999.0f, 9999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "エミッター座標を基準としたオフセット。\n"
                    "(0, 0, 0) でエミッターの中心に引き戻す。\n"
                    "C++側で毎フレーム\n"
                    "  pSettingsData_->curlNoiseAttractCenter =\n"
                    "      emitterPos + offset;\n"
                    "として渡すこと。");
            ImGui::PopStyleColor();

            // --------------------------------------------------
            // プリセット
            // --------------------------------------------------
            ImGui::Spacing();
            ImGui::TextDisabled("プリセット:");
            ImGui::SameLine();
            if (ImGui::SmallButton("煙・霧"))
            {
                pSettingsData_->curlNoiseScale = 0.4f;
                pSettingsData_->curlNoiseStrength = 1.5f;
                pSettingsData_->curlNoiseTimeScale = 0.15f;
                pSettingsData_->curlNoiseOctaves = 2;
                pSettingsData_->curlNoiseAttractStrength = 0.3f;
                pSettingsData_->curlNoiseBlendMode = 0;
                pSettingsData_->curlNoisePosRandomStrength = 0.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("炎・乱流"))
            {
                pSettingsData_->curlNoiseScale = 1.2f;
                pSettingsData_->curlNoiseStrength = 4.0f;
                pSettingsData_->curlNoiseTimeScale = 0.6f;
                pSettingsData_->curlNoiseOctaves = 3;
                pSettingsData_->curlNoiseAttractStrength = 0.8f;
                pSettingsData_->curlNoiseBlendMode = 0;
                pSettingsData_->curlNoisePosRandomStrength = 0.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("水流"))
            {
                pSettingsData_->curlNoiseScale = 0.7f;
                pSettingsData_->curlNoiseStrength = 2.5f;
                pSettingsData_->curlNoiseTimeScale = 0.25f;
                pSettingsData_->curlNoiseOctaves = 2;
                pSettingsData_->curlNoiseAttractStrength = 0.5f;
                pSettingsData_->curlNoiseBlendMode = 0;
                pSettingsData_->curlNoisePosRandomStrength = 0.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("一点放射"))
            {
                // 小さいエミッタから四方八方に広がる演出向けプリセット
                pSettingsData_->curlNoiseScale = 0.6f;
                pSettingsData_->curlNoiseStrength = 2.0f;
                pSettingsData_->curlNoiseTimeScale = 0.2f;
                pSettingsData_->curlNoiseOctaves = 2;
                pSettingsData_->curlNoiseAttractStrength = 0.0f;
                pSettingsData_->curlNoiseBlendMode = 0;
                pSettingsData_->curlNoisePosRandomStrength = 1.5f;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("小さいエミッタから四方八方に広がる演出向け\n分散強度: 1.5 を設定します");
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- トレイル ----
    if (pSettingsData_->enableTrail &&
        effectHeader("トレイル", ImVec4(0.2f, 0.7f, 0.35f, 1.0f),
                     [&] { pSettingsData_->enableTrail = 0; }))
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);

        bool v = pSettingsData_->enableTrail != 0;

        if (v)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));

            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("基本設定");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::DragFloat("生成間隔距離##tsd", &pSettingsData_->trailSpawnDistance, 0.01f, 0.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("この距離ごとにトレイルを生成\n小さいほど滑らか（推奨: 0.05-0.15）");
            int maxT = static_cast<int>(pSettingsData_->maxTrailPerParticle);
            if (ImGui::DragInt("最大数/親##tmax", &maxT, 1, 1, 1000))
                pSettingsData_->maxTrailPerParticle = static_cast<uint32_t>(maxT);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("トレイル特性");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::DragFloat("寿命倍率##tlt", &pSettingsData_->trailLifeTimeScale, 0.05f, 0.0f, 999.0f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("親の残り寿命に対する倍率（推奨: 0.8-1.5）");
            ImGui::DragFloat("最小寿命(s)##tmn", &pSettingsData_->trailMinLifeTime, 0.05f, 0.0f, 999.0f, "%.4f");
            ImGui::DragFloat3("スケール倍率##tsc", &pSettingsData_->trailScaleMultiplier.x, 0.01f, 0.0f, 999.0f, "%.4f");
            ImGui::ColorEdit4("色倍率##tco", &pSettingsData_->trailColorMultiplier.x);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("速度設定");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::PopStyleColor(); // FrameBg

            bool inh = pSettingsData_->trailInheritVelocity != 0;
            if (ImGui::Checkbox("親の速度を継承##tiv", &inh))
                pSettingsData_->trailInheritVelocity = inh ? 1 : 0;
            if (inh)
            {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));
                ImGui::DragFloat("速度倍率##tvs", &pSettingsData_->trailVelocityScale, 0.01f, 0.0f, 99.0f, "%.4f");
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }
        }

        ImGui::PopStyleColor(); // CheckMark
        ImGui::Unindent();
    }

    // =======================================================
    // 10. Debug Info（常時表示）
    // =======================================================
    static const int kHistorySize = 256;

    ImGui::Spacing();
    ImGui::Separator();
    {
        int32_t headVal = 0, tailVal = 0;
        int32_t *p = nullptr;
        D3D12_RANGE r = {0, sizeof(int32_t)};
        if (SUCCEEDED(freeListIndexReadbackBuffer_->Map(0, &r, reinterpret_cast<void **>(&p))))
        {
            headVal = *p;
            freeListIndexReadbackBuffer_->Unmap(0, nullptr);
        }
        if (SUCCEEDED(freeListTrailIndexReadbackBuffer_->Map(0, &r, reinterpret_cast<void **>(&p))))
        {
            tailVal = *p;
            freeListTrailIndexReadbackBuffer_->Unmap(0, nullptr);
        }
        int32_t used = pSettingsData_->maxParticleCount - (tailVal - headVal);
        float rate = static_cast<float>(used) / static_cast<float>(pSettingsData_->maxParticleCount);

        static float particleHistory[kHistorySize] = {};
        static float particleRateHistory[kHistorySize] = {};
        static int histOffset = 0;
        particleHistory[histOffset] = static_cast<float>(used);
        particleRateHistory[histOffset] = rate * 100.0f;
        histOffset = (histOffset + 1) % kHistorySize;

        char overlay[64];
        sprintf_s(overlay, "%d / %d  (%.1f%%)", used, static_cast<int>(pSettingsData_->maxParticleCount), rate * 100.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              rate >= 0.9f   ? ImVec4(1.0f, 0.3f, 0.3f, 1)
                              : rate >= 0.7f ? ImVec4(1.0f, 0.9f, 0.2f, 1)
                                             : ImVec4(0.4f, 1.0f, 0.4f, 1));
        ImGui::ProgressBar(rate, ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
        ImGui::TextUnformatted("パーティクル数 (履歴)");
        ImGui::PopStyleColor();

        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.05f, 0.05f, 0.09f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));

        if (ImPlot::BeginPlot("##ParticleCount", ImVec2(-1, 80),
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoInputs |
                                  ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText))
        {
            ImPlot::SetupAxes(nullptr, nullptr,
                              ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines,
                              ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHistorySize, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, static_cast<double>(pSettingsData_->maxParticleCount), ImPlotCond_Always);
            ImPlot::PlotLine("##pc", particleHistory, kHistorySize, 1.0, 0.0,
                             ImPlotLineFlags_None, histOffset);
            ImPlot::EndPlot();
        }

        ImPlot::PopStyleColor(3);
    }

    ImGui::PopItemWidth();

    if (pSettingsData_->enableGather)
        LineRenderer::GetInstance()->AddSphere(pSettingsData_->gatherTarget, 0.1f, {1.0f, 0.0f, 1.0f, 1.0f}, 8);
    if (pSettingsData_->enableVortex)
    {
        LineRenderer::GetInstance()->AddSphere(pSettingsData_->vortexTarget, 0.1f, {0.5f, 1.0f, 0.0f, 1.0f}, 8);
        // 解決済みの回転軸（＝渦の向き）。基準空間を変えると、この線がエミッター/カメラに追従する。
        const float axisLen = pSettingsData_->vortexAxis.Length();
        if (axisLen > 1e-6f)
        {
            const Vector3 axis = pSettingsData_->vortexAxis / axisLen;
            LineRenderer::GetInstance()->AddLine(pSettingsData_->vortexTarget - axis,
                                                 pSettingsData_->vortexTarget + axis,
                                                 {0.5f, 1.0f, 0.0f, 1.0f});
        }
    }

#endif // USE_IMGUI
}
} // namespace Hagine
