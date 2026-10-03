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

// ParticleCSGroup のエディタUI: DrawImGui 本体と基本セクション
// （出現・寿命・サイズ／速度・色彩・ブレンド／テクスチャ）。
// エフェクトカードは ParticleCSGroupImGuiEffect.cpp、
// 発生形状・場系・Debug Info は ParticleCSGroupImGuiField.cpp、
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
} // namespace
#endif

void ParticleCSGroup::DrawImGui()
{
#ifdef USE_IMGUI
    if (!pSettingsData_)
        return;

    ImGui::PushItemWidth(-120.0f);

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
    // セクションが多いので、まとめて開閉できるようにする（要求は各見出しの PushSectionColor が拾う）
    ImGui::SameLine();
    if (NeutralButton("すべて開く##pcsOpenAll"))
        g_particleSectionOpenRequest = 1;
    ImGui::SameLine();
    if (NeutralButton("すべて閉じる##pcsCloseAll"))
        g_particleSectionOpenRequest = 0;
    ImGui::Spacing();

    DrawImGuiBasicSection();
    DrawImGuiAppearanceSection();
    DrawImGuiTextureSection();
    DrawImGuiEffectSection();
    DrawImGuiEmitShapeSection();
    DrawImGuiFieldEffectSection();
    DrawImGuiDebugSection();
    g_particleSectionOpenRequest = -1;

    ImGui::PopItemWidth();

    LineCategoryScope lineScope(LineCategory::Particle);
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

#ifdef USE_IMGUI
void ParticleCSGroup::DrawImGuiBasicSection()
{
    // =======================================================
    // 1. 出現・寿命・サイズ（赤系）【コア・常設】
    // =======================================================
    PushSectionColor(DebugTheme::kAccentRed);
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

}

void ParticleCSGroup::DrawImGuiAppearanceSection()
{
    // =======================================================
    // 2. 速度・色彩・ブレンド（青系）
    // =======================================================
    PushSectionColor(DebugTheme::kAccentBlue);
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

}

void ParticleCSGroup::DrawImGuiTextureSection()
{
    // =======================================================
    // 2.5 テクスチャ（画像差し替え・水色系）
    // =======================================================
    PushSectionColor(DebugTheme::kAccentCyan);
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

    // =======================================================
    // プロシージャル形状（画像を使わずに形を作る・水色系）
    // =======================================================
    ImGui::Spacing();
    PushSectionColor(DebugTheme::kAccentCyan);
    bool openShape = ImGui::CollapsingHeader("  形を計算で作る（画像なし）");
    PopSectionColor();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("テクスチャを使わず、ピクセルシェーダーで**形そのものを計算して**描きます。\n"
                          "画像を1枚も用意せずに、輪郭のある炎・稲妻・衝撃波が出せます。\n\n"
                          "丸いソフトスプライトを大量に飛ばしても格闘ゲームの炎にはなりません。\n"
                          "「少ない枚数を大きく出す」のがコツです。");
    }
    if (openShape)
    {
        ImGui::Indent();
        int sm = static_cast<int>(pSettingsData_->shapeMode);
        const char *kShapes[] = {"テクスチャをそのまま（従来）", "炎", "シャード（稲妻・尖った破片）",
                                 "リング（衝撃波）", "メッシュ炎（円錐・円柱に貼る）"};
        ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentBlue));
        if (ImGui::Combo("形##shapeMode", &sm, kShapes, IM_ARRAYSIZE(kShapes)))
            pSettingsData_->shapeMode = static_cast<uint32_t>(sm);
        ImGui::PopStyleColor();

        if (pSettingsData_->shapeMode != 0u)
        {
            ImGui::DragFloat("輪郭のしきい値##shapeEdge", &pSettingsData_->shapeEdge, 0.005f, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("大きいほど痩せ、小さいほど太くなります。");

            ImGui::DragFloat("輪郭のぼかし##shapeSoft", &pSettingsData_->shapeSoftness, 0.002f, 0.001f, 0.5f, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("小さいほどパキっとしたセル調の縁になります。\nアニメ調にしたいなら 0.02 以下。");

            ImGui::DragFloat("ゆらぎの細かさ##shapeNoise", &pSettingsData_->shapeNoiseScale, 0.05f, 0.1f, 20.0f, "%.2f");
            ImGui::DragFloat("ゆらぎの速さ##shapeSpeed", &pSettingsData_->shapeSpeed, 0.05f, 0.0f, 20.0f, "%.2f");

            ImGui::DragFloat("縁取りの太さ##shapeRimW", &pSettingsData_->shapeRimWidth, 0.005f, 0.0f, 1.0f, "%.3f");
            ImGui::ColorEdit4("縁の色##shapeRimC", &pSettingsData_->shapeRimColor.x,
                              ImGuiColorEditFlags_AlphaBar);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("中心は白く抜け、外周だけこの色になります（DB系の2トーンの見え方）。");

            if (pSettingsData_->shapeMode == 4u)
            {
                ImGui::TextColored(DebugTheme::kAccentBlue,
                                   "※ このモードは板ポリではなく**円錐/円柱メッシュ**に貼る前提です");
                ImGui::TextDisabled("　グループの形を「円錐」にして、ビルボードを切り、");
                ImGui::TextDisabled("　下の「UVを流す速さ」のYをマイナスにすると炎が立ち上ります");
            }
            ImGui::TextColored(DebugTheme::kAccentOrange,
                               "※ 1画素あたりの計算が増えます。大きい粒を少数だけ出す使い方に向きます");
        }

        ImGui::DragFloat("縁の発光（フレネル）##shapeFresnel", &pSettingsData_->shapeFresnel, 0.01f, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("輪郭ほど濃く、カメラ正面を向いた面ほど薄くします。\n"
                              "体を包む殻メッシュに使うと、中のキャラが透けて輪郭だけが光るオーラになります。\n"
                              "面の向きが要るので、ビルボードを切ったメッシュにだけ効きます。0 で無効。");
        }
        ImGui::Unindent();
    }

    // =======================================================
    // 地物との馴染ませ／UVスクロール（水色系）
    // =======================================================
    ImGui::Spacing();
    PushSectionColor(DebugTheme::kAccentCyan);
    bool openBlendIn = ImGui::CollapsingHeader("  地面との馴染ませ・絵を流す");
    PopSectionColor();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("板ポリが地面や壁へ**刺さった断面が直線で見える**のを消し、\n"
                          "絵（テクスチャ／計算で作った形）を流して動いて見せます。\n\n"
                          "煙・土埃・地面のオーラなど「地物に接するもの」は基本ONにしてください。");
    }
    if (openBlendIn)
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentBlue);

        bool softOn = pSettingsData_->enableSoftParticle != 0;
        if (ImGui::Checkbox("刺さった断面を消す（ソフトパーティクル）##softP", &softOn))
            pSettingsData_->enableSoftParticle = softOn ? 1u : 0u;
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("背景との奥行きの差が近いほど薄くして、境目を溶かします。\n"
                              "ONにしたグループが1つでもあるフレームだけ深度を複製するので、\n"
                              "使わないシーンでは費用ゼロです。");
        }
        if (softOn)
        {
            ImGui::DragFloat("消え始める距離##softFade", &pSettingsData_->softParticleFade,
                             0.02f, 0.01f, 20.0f, "%.2f");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("背景までこの距離まで近づくと完全に消えます（ワールド単位）。\n"
                                  "粒のサイズと同じくらいから始めるのが目安。\n"
                                  "大きくしすぎると地面付近の粒が丸ごと消えます。");
            }
        }

        ImGui::Spacing();
        ImGui::DragFloat2("UVを流す速さ##uvScroll", &pSettingsData_->uvScrollSpeed.x, 0.01f, -5.0f, 5.0f, "%.3f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("毎秒どれだけUVをずらすか。テクスチャでも計算で作った形でも効きます。\n"
                              "縦（Y）をマイナスにすると絵が上へ流れるので、炎が立ち上る動きになります。\n"
                              "0.2〜0.6 くらいから試すと良いです。");
        }

        ImGui::Spacing();
        ImGui::DragFloat("発光の強さ##emissive", &emissive_, 0.05f, 0.0f, 20.0f, "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("色全体に掛ける倍率です。粒子の色は1までしか持てないので、\n"
                              "ブルームで光をにじませたい閃光・火花はここを 2〜5 に上げます。\n"
                              "1 で従来どおり。煙や破片は 1 のままにしてください（光ってしまう）。");
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // =======================================================
    // フリップブック（スプライトシート・水色系）
    // =======================================================
    ImGui::Spacing();
    PushSectionColor(DebugTheme::kAccentBlue);
    bool openFlip = ImGui::CollapsingHeader("  フリップブック（コマ送りの絵）");
    PopSectionColor();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("1枚の画像に格子状に並べたコマを順に切り替えて、**絵として動く**エフェクトにします。\n"
                          "炎・爆発・稲妻・オーラのような「形のあるもの」は、\n"
                          "小さい点を大量に飛ばすよりこちらの方が近い見た目になります。\n\n"
                          "使い方: 4x4 なら16コマの画像を1枚用意して、横4・縦4 を入れるだけ。");
    }
    if (openFlip)
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentBlue);

        bool flipOn = pSettingsData_->enableFlipbook != 0;
        if (ImGui::Checkbox("コマ送りを使う##fbEnable", &flipOn))
            pSettingsData_->enableFlipbook = flipOn ? 1u : 0u;

        if (flipOn)
        {
            int cols = static_cast<int>(pSettingsData_->flipbookCols);
            int rows = static_cast<int>(pSettingsData_->flipbookRows);
            if (ImGui::DragInt("横のコマ数##fbCols", &cols, 1, 1, 64))
                pSettingsData_->flipbookCols = static_cast<uint32_t>(cols < 1 ? 1 : cols);
            if (ImGui::DragInt("縦のコマ数##fbRows", &rows, 1, 1, 64))
                pSettingsData_->flipbookRows = static_cast<uint32_t>(rows < 1 ? 1 : rows);
            ImGui::TextColored(DebugTheme::kTextDim, "合計 %d コマ",
                               static_cast<int>(pSettingsData_->flipbookCols * pSettingsData_->flipbookRows));

            int mode = static_cast<int>(pSettingsData_->flipbookMode);
            const char *kModes[] = {"寿命で1周（爆発・着弾向け）", "fps でループ（炎・オーラ向け）"};
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentBlue));
            if (ImGui::Combo("再生のしかた##fbMode", &mode, kModes, IM_ARRAYSIZE(kModes)))
                pSettingsData_->flipbookMode = static_cast<uint32_t>(mode);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("寿命で1周: 生まれてから消えるまでで全コマを1回だけ再生する。\n"
                                  "           爆発・着弾のように「1回きり」の絵に使う。\n\n"
                                  "fps ループ: 時間でずっと回し続ける。\n"
                                  "           炎・オーラのように「出続ける」絵に使う。");
            }

            if (pSettingsData_->flipbookMode == 1u)
            {
                ImGui::DragFloat("コマ送り速度(fps)##fbFps", &pSettingsData_->flipbookFps, 0.5f, 0.0f, 120.0f, "%.0f");
            }

            bool randomStart = pSettingsData_->flipbookRandomStart != 0;
            if (ImGui::Checkbox("粒ごとに開始コマをずらす##fbRand", &randomStart))
                pSettingsData_->flipbookRandomStart = randomStart ? 1u : 0u;
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("同時に出た粒が全部同じ絵にならないよう、開始コマを散らします。\n"
                                  "煙や破片など「たくさん出すもの」は基本ON。\n"
                                  "1枚の大きなオーラのように1粒だけ出す場合はOFF。");
            }

            ImGui::TextColored(DebugTheme::kTextDim,
                               "※ コマ番号は1粒ごとに持つので、粒を増やしても描画の負荷は変わりません");
        }

        ImGui::PopStyleColor();
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

}
#endif // USE_IMGUI
} // namespace Hagine
