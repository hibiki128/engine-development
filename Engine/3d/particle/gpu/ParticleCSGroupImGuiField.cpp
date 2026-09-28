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

// ParticleCSGroup のエディタUI: 発生形状・場系エフェクト・Debug Info。
// エフェクトカードは ParticleCSGroupImGuiEffect.cpp、
// DrawImGui 本体と基本セクションは ParticleCSGroupImGui.cpp にある。
namespace Hagine {
#ifdef USE_IMGUI

void ParticleCSGroup::DrawImGuiEmitShapeSection()
{
    // =======================================================
    // 発生形状（ピンク系）【コア・常設】
    // =======================================================
    PushSectionColor(DebugTheme::kAccentRed);
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
}

void ParticleCSGroup::DrawImGuiFieldEffectSection()
{

    // ---- ギャザー（集合）----
    if (pSettingsData_->enableGather &&
        EffectHeader("ギャザー（集合）", DebugTheme::kAccentPurple,
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
            EffectSpaceCombo("gatherSpace", pSettingsData_);
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
        EffectHeader("渦巻き（Vortex）", DebugTheme::kAccentCyan,
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

            EffectSpaceCombo("vortexSpace", pSettingsData_);
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
        EffectHeader("カールノイズ", DebugTheme::kAccentCyan,
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
        EffectHeader("トレイル", DebugTheme::kAccentGreen,
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

}

void ParticleCSGroup::DrawImGuiDebugSection()
{
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
}

#endif // USE_IMGUI
} // namespace Hagine
