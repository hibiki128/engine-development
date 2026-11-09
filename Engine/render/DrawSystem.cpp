#include "DrawSystem.h"
#ifdef USE_IMGUI
#include <edit/play/PlayModeManager.h>
#endif
#include "DirectXCommon.h"
#include "collider/CollisionManager.h"
#include "debug/profiler/CpuProfiler.h"
#include <debug/capture/CaptureManager.h>
#include "debug/profiler/GpuProfiler.h"
#include "data/DataHandler.h"
#include "bloom/BloomPass.h"
#include "volumetric/VolumetricFogPass.h"
#include "deferred/DeferredRenderer.h"
#include "light/LightGroup.h"
#include "object/Object3d.h"
#include "object/Object3dInstancing.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include "graphics/srv/SrvManager.h"
#include "SceneViewRenderer.h"
#include "particle/ParticleEditor.h"
#include "render/RenderCulling.h"
#include "particle/gpu/ParticleCSEmitter.h"
#include "particle/gpu/ParticleCSSpawner.h"
#include "scene/SceneManager.h"
#include <shadow/ShadowMap.h>
#include "render/CameraFade.h"
#include "raytracing/RtAoPass.h"
#include "ssao/SsaoRenderer.h"
#include <algorithm>
#ifdef USE_IMGUI
#include "particle/gpu/ParticleCSEditor.h"
#endif
#ifdef USE_IMGUI
#include "imgui.h"
#include "line/LineRenderer.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <vector>
#endif

namespace Hagine {
void DrawSystem::Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager,
                            OffScreen *pOffScreen, SceneManager *sceneManager,
                            CollisionManager *collision)
{
    pDxCommon_ = pDxCommon;
    pSrvManager_ = pSrvManager;
    pSceneManager_ = sceneManager;
    pCollision_ = collision;

    stageOffScreens_[0] = pOffScreen;
    nextStageIndex_ = 1;
}

// -------------------------------------------------------
// 描画エントリ登録
// -------------------------------------------------------

void DrawSystem::RegisterImpl(std::string name, int stageIndex,
                              std::function<void(const ViewProjection &)> drawFunc)
{
    for (auto &e : entries_)
    {
        if (e.name == name)
        {
            e.stageIndex = stageIndex;
            e.draw = std::move(drawFunc);
            return;
        }
    }
    entries_.push_back({std::move(name), stageIndex, std::move(drawFunc), true});
}

void DrawSystem::Register(std::string name, DrawLayer layer,
                          std::function<void(const ViewProjection &)> drawFunc)
{
    int stage = (layer == DrawLayer::PostEffect) ? kUILayer : static_cast<int>(layer);
    RegisterImpl(std::move(name), stage, std::move(drawFunc));
}

void DrawSystem::Register(std::string name, int stageIndex,
                          std::function<void(const ViewProjection &)> drawFunc)
{
    RegisterImpl(std::move(name), stageIndex, std::move(drawFunc));
}

void DrawSystem::Unregister(const std::string &name)
{
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&name](const DrawEntry &e) { return e.name == name; }),
        entries_.end());
}

void DrawSystem::Clear()
{
    entries_.clear();
}

// -------------------------------------------------------
// マルチステージ管理
// -------------------------------------------------------

int DrawSystem::CreateStage()
{
    int idx = nextStageIndex_++;
    auto owned = std::make_unique<OffScreen>();
    owned->Initialize();
    stageOffScreens_[idx] = owned.get();
    ownedOffScreens_.push_back(std::move(owned));
    return idx;
}

OffScreen *DrawSystem::GetStageOffScreen(int stageIndex)
{
    auto it = stageOffScreens_.find(stageIndex);
    return (it != stageOffScreens_.end()) ? it->second : nullptr;
}

// -------------------------------------------------------
// メイン描画パイプライン
// -------------------------------------------------------

void DrawSystem::Draw(const ViewProjection &vp)
{
    // GPU プロファイラ: フレーム先頭で ring を進め、過去フレームの結果を取り込む
    GpuProfiler::GetInstance()->BeginFrame();
    GpuProfiler::GetInstance()->BeginPipelineStats(pDxCommon_->GetCommandList().Get());

    // オブジェクトのインスタンシング描画: インスタンスバッファの書き込み位置をフレーム先頭で戻す。
    // 影 / G-Buffer / 前方描画で内容が違うので、1フレーム内では領域を使い回さない。
    Object3dInstancing::GetInstance()->BeginFrame();

    // 錐台カリング: このフレームの判定数・省いた数を数え直す
    RenderCulling::BeginFrame();

    // ─── GPU パーティクル Compute フェーズ（全エミッターを一括実行して Direct Queue に Wait 挿入）───
    {
        HAGINE_CPU_PROFILE("DS/ParticleCompute+Wait");
        for (auto &entry : entries_)
        {
            if (entry.enabled && entry.stageIndex == kGPUParticleCompute)
            {
                entry.draw(vp);
            }
        }
#ifdef USE_IMGUI
        // GPUパーティクルエディタのエミッターをシーン非依存でシミュレートする。
        // 描画はプレビュー窓(RenderPreview)の専用VPだけが行うためゲームシーンには漏れない。
        // （Compute=シミュレーションのみここで実行。Graphics はプレビューに隔離済み）
        ParticleCSEditor::GetInstance()->DrawAllCompute(vp);
#endif
        // 実行時にシーンへ置かれた GPU パーティクル（ParticleCSSpawner 所有）。
        // エディタのものと違いゲーム画面にも描画するので、USE_IMGUI に閉じず常に実行する。
        // ここで登録不要にしているのは、シーン遷移で entries_ が Clear() されるため。
        //
        // これはゲーム世界のシミュレーションなので、一時停止・停止中は進めない
        // （止めないと発生・移動が続いてしまう。描画は別なので見た目は止まったまま残る）。
#ifdef USE_IMGUI
        if (PlayModeManager::GetInstance()->ShouldUpdateGame())
#endif // USE_IMGUI
        {
            ParticleCSSpawner::GetInstance()->DrawCompute(vp);
        }

        // Compute スパンを Execute 前に resolve（リストが閉じる前に記録する必要がある）
        GpuProfiler::GetInstance()->ResolveCompute(pDxCommon_->GetComputeCommandList().Get());

        // 記録が無ければ ExecuteComputeCommands は自己ガードで no-op、Wait も signaled 済み値への待ちで無害。
        pDxCommon_->ExecuteComputeCommands();
        pDxCommon_->WaitForComputeOnDirectQueue();
    }

#ifdef USE_IMGUI
    // ─── GPUパーティクル プレビュー窓を描画（Compute 完了後・ステージ束ね前）───
    // Compute 済みの生存バッファを VS 読み取り可能な状態のままプレビューVPで再描画する。
    // 後段のステージループ(PreRenderTexture)がオフスクリーンRTと全画面ビューポートを束ね直すため復元不要。
    ParticleCSEditor::GetInstance()->RenderPreview();
#endif

    // ─── スキニング（このフレームに動いた体の全員分をまとめて）───
    // 影パスより前に済ませ、影・本描画・カメラビューのどれもが今のポーズを使えるようにする
    {
        HAGINE_CPU_PROFILE("DS/Skinning");
        ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();
        pSrvManager_->SetDescriptorHeap();
        const int gpuSkinning = GpuProfiler::GetInstance()->OpenGraphics(pCommandList, "Skinning");
        Object3d::FlushPendingSkinning(pCommandList);
        GpuProfiler::GetInstance()->Close(pCommandList, gpuSkinning);
    }

    // ─── シャドウプレパス ───
    {
        HAGINE_CPU_PROFILE("DS/ShadowRec");
        ShadowMap *shadowMap = ShadowMap::GetInstance();
        shadowMap->Update(); // 有効フラグをGPUバッファに反映（無効時もenabledを0にするため毎フレーム呼ぶ）
        if (shadowMap->IsEnabled())
        {
            shadowMap->BeginShadowPass();
            pSrvManager_->SetDescriptorHeap();
            shadowMap->SetShadowPassActive(true);
            int gpuShadow = GpuProfiler::GetInstance()->OpenGraphics(pDxCommon_->GetCommandList().Get(), "Shadow");
            for (auto &entry : entries_)
            {
                if (entry.enabled && entry.stageIndex == 0)
                {
                    entry.draw(vp);
                }
            }
            GpuProfiler::GetInstance()->Close(pDxCommon_->GetCommandList().Get(), gpuShadow);
            shadowMap->SetShadowPassActive(false);
            shadowMap->EndShadowPass();
        }
    }

#ifdef USE_IMGUI
    // ─── カメラビュー窓（好きなカメラから見たシーン）───
    // 影を使うので影の後、線はメインの描画で消えるのでメインの前。
    // 後に続くステージループが描画先とビューポートを張り直すので、戻す必要は無い
    {
        HAGINE_CPU_PROFILE("DS/CameraViews");
        SceneViewRenderer::GetInstance()->Render();
    }
#endif

    // 登録済みステージ（kUILayer を除く）を昇順で処理
    std::vector<int> sortedStages;
    for (auto &[idx, _] : stageOffScreens_)
    {
        sortedStages.push_back(idx);
    }
    std::sort(sortedStages.begin(), sortedStages.end());

    // GPUパーティクルの発光など、Compute フェーズで登録された動的ポイントライトを
    // ここで定数バッファへ反映する。以降のシーン描画が同じフレームの光を拾える。
    LightGroup *lightGroup = LightGroup::GetInstance();
    lightGroup->CommitPointLights();

    // ─── 粒子1個1個の光源化（ディファードON時のみ）───
    // CPU側のライトをGPUバッファへ転送してから、生存粒子から光源を追記する。
    // パーティクルの Compute は上で Execute + Wait 済みなので、生存バッファは確定している。
    // ライトの中身はステージ共通なので、ステージループより前に1回だけ行う。
    if (DeferredRenderer::GetInstance()->IsEnabled())
    {
        HAGINE_CPU_PROFILE("DS/ParticleLights");
        ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();
        int gpuLightGen = GpuProfiler::GetInstance()->OpenGraphics(pCommandList, "ParticleLightGen");
        lightGroup->BeginGpuLightAppend(pCommandList);
        ParticleCSEmitter::SubmitAllParticleLights(vp, pCommandList);
        lightGroup->EndGpuLightAppend(pCommandList);
        GpuProfiler::GetInstance()->Close(pCommandList, gpuLightGen);
    }

    OffScreen *lastOffScreen = nullptr;

    // ステージループ（本描画の記録＋ポストエフェクト）を計測。
    // lastOffScreen は後段で使うのでブロック外で宣言している。
    {
        HAGINE_CPU_PROFILE("DS/StageLoop(scene+postfx)");
        for (size_t si = 0; si < sortedStages.size(); ++si)
        {
            int stageIdx = sortedStages[si];
            OffScreen *stageOS = stageOffScreens_.at(stageIdx);

            // ─── オフスクリーンテクスチャへ描画 ───
            pDxCommon_->PreRenderTexture();
            pSrvManager_->SetDescriptorHeap();

            // 前ステージの結果を背景として合成
            if (lastOffScreen)
            {
                stageOS->BlitToOffScreen(lastOffScreen->GetFinalResultSrvIndex());
            }

            DeferredRenderer *deferred = DeferredRenderer::GetInstance();

            // ─── ディファード: G-Buffer パス ───
            // 不透明の Object3d だけがここに描かれる（Object3d 側がパス状態を見て分岐する）。
            // スカイボックス・スプライト・パーティクル・線は後段の前方描画へ回る。
            if (deferred->IsEnabled())
            {
                HAGINE_CPU_PROFILE("DS/GBuffer");
                int gpuGBuffer = GpuProfiler::GetInstance()->OpenGraphics(pDxCommon_->GetCommandList().Get(), "G-Buffer");
                deferred->BeginGBufferPass(vp);
                for (auto &entry : entries_)
                {
                    if (entry.enabled && entry.stageIndex == stageIdx)
                    {
                        entry.draw(vp);
                    }
                }
                deferred->EndGBufferPass();
                GpuProfiler::GetInstance()->Close(pDxCommon_->GetCommandList().Get(), gpuGBuffer);

                int gpuLighting = GpuProfiler::GetInstance()->OpenGraphics(pDxCommon_->GetCommandList().Get(), "Deferred(cull+lighting)");
                deferred->CullLights();
                deferred->RenderLighting();
                deferred->BeginForwardPass();
                GpuProfiler::GetInstance()->Close(pDxCommon_->GetCommandList().Get(), gpuLighting);
            }

            int gpuScene = GpuProfiler::GetInstance()->OpenGraphics(pDxCommon_->GetCommandList().Get(), "Scene(不透明+UI等)");
            for (auto &entry : entries_)
            {
                if (entry.enabled && entry.stageIndex == stageIdx)
                {
                    entry.draw(vp);
                }
            }
            GpuProfiler::GetInstance()->Close(pDxCommon_->GetCommandList().Get(), gpuScene);

            // ソフトパーティクル用に深度を複製しておく（要求があったフレームだけ動く）。
            // 不透明を描き終えた今が一番正しい深度で、粒子を描く前でないと意味が無い
            pDxCommon_->CaptureDepthForRead();

            // ボリュメトリックフォグ。不透明物の深度が揃った後、パーティクルとブルームの前に掛ける。
            // 重ねるステージには掛けない
            if (stageIdx == 0)
            {
                int gpuFog = GpuProfiler::GetInstance()->OpenGraphics(pDxCommon_->GetCommandList().Get(), "VolumetricFog");
                VolumetricFogPass::GetInstance()->Render(vp, pDxCommon_->GetOffScreenResource());
                GpuProfiler::GetInstance()->Close(pDxCommon_->GetCommandList().Get(), gpuFog);
            }

            // 「パーティクルだけを光らせる」ために、粒子を描く直前のシーンを控える。
            // 描画後との差がパーティクルの寄与そのものになる（無効時は何もしない）
            BloomPass::GetInstance()->CaptureBeforeParticles(pDxCommon_->GetOffScreenResource());

            // 実行時にシーンへ置かれた GPU パーティクルを、このステージに属するものだけ描画する。
            // （どのステージかは各エミッターの drawGroup から DrawGroupManager::StageOf で決まる）
            ParticleCSSpawner::GetInstance()->DrawGraphics(vp, stageIdx);

            // ブルーム。デバッグ線やUIより前に掛けるので、線は光らない。
            // トーンマップはポストエフェクトの最後なので、ここでは HDR のまま足している
            {
                int gpuBloom = GpuProfiler::GetInstance()->OpenGraphics(pDxCommon_->GetCommandList().Get(), "Bloom");
                BloomPass::GetInstance()->Render(pDxCommon_->GetOffScreenResource());
                GpuProfiler::GetInstance()->Close(pDxCommon_->GetCommandList().Get(), gpuBloom);
            }

            // 注: GPUパーティクルエディタのエミッターは「プレビュー窓のみ」で確認する。
            // 以前はここで DrawAllGraphics(vp) を呼び現在のシーン pOffScreen にも描画していたが、
            // 編集中のパーティクルがゲームシーンに漏れて見えてしまうため撤去。
            // シミュレーション（DrawAllCompute）は上のフェーズで実行済みで、
            // 描画は RenderPreview()（プレビュー専用VP）だけが行う。

#ifdef USE_IMGUI
            if (stageIdx == 0)
            {
                // コライダーの線を積んでから描く（旧実装は順序が逆で1フレーム遅れていた）
                pCollision_->DebugDraw(vp);
                LineRenderer::GetInstance()->Render(vp);
            }
#endif

            // ─── ポストエフェクト適用 → finalResult（コピーなし）───
            if (si == 0)
            {
                pDxCommon_->PreDraw(); // 初回: バックバッファも遷移
            }
            else
            {
                pDxCommon_->PreDrawForEffects(); // 2回目以降: バックバッファ遷移なし
            }
            // フォグは深度からワールド座標まで戻すので、射影行列だけでなく
            // ビュー行列・カメラ位置・太陽の向きもまとめて渡す
            stageOS->SetCamera(vp.matView_, vp.matProjection_, vp.translation_,
                               lightGroup->GetDirectionalLightDirection());
            // HDR を画面の範囲へ収めるトーンマップは、最後のステージで一度だけ掛ける。
            // 中間ステージの結果は次のステージの背景として重ねられるので、
            // そこで掛けると背景にだけ二重に掛かって暗く沈む
            stageOS->SetApplyToneMap(si + 1 == sortedStages.size());
            stageOS->DrawWithoutCopy();
            pDxCommon_->TransitionDepthBarrier();

            lastOffScreen = stageOS;
        }
    } // DS/StageLoop

    if (!lastOffScreen)
    {
        GpuProfiler::GetInstance()->EndPipelineStats(pDxCommon_->GetCommandList().Get());
        GpuProfiler::GetInstance()->ResolveGraphics(pDxCommon_->GetCommandList().Get());
        ParticleEditor::GetInstance()->UpdateFrameStats();
        return;
    }

    // ─── UI・シーン遷移を finalResult に合成 ───
    {
        HAGINE_CPU_PROFILE("DS/Composite+Copy");
        lastOffScreen->BeginCompositePass();
        pSrvManager_->SetDescriptorHeap();

        for (auto &entry : entries_)
        {
            if (entry.enabled && entry.stageIndex == kUILayer)
            {
                entry.draw(vp);
            }
        }

        // drawGroup が "UI" の実行時 GPU パーティクル
        ParticleCSSpawner::GetInstance()->DrawGraphics(vp, kUILayer);

        lastOffScreen->EndCompositePass();

        // シーン遷移は最前面（UIの上）。合成し終えた画を読んで幕を重ね、同じ画へ書き戻す
        {
            int gpuTransition = GpuProfiler::GetInstance()->OpenGraphics(pDxCommon_->GetCommandList().Get(), "SceneTransition");
            pSceneManager_->DrawTransition(lastOffScreen->GetFinalResultResource());
            GpuProfiler::GetInstance()->Close(pDxCommon_->GetCommandList().Get(), gpuTransition);
        }

        // ─── finalResult（フルフレーム）をバックバッファへコピー ───
        lastOffScreen->CopyFinalResultToBackBuffer();

        // スクリーンショットの「ゲーム画面」は、ポストエフェクト・UI・遷移まで済んだこの画を撮る
        CaptureManager::GetInstance()->SetGameViewResource(lastOffScreen->GetFinalResultResource());
    } // DS/Composite+Copy

    // Graphics スパンと描画統計を resolve（描画コマンド記録が全て済んだ後・リスト Close 前）
    GpuProfiler::GetInstance()->EndPipelineStats(pDxCommon_->GetCommandList().Get());
    GpuProfiler::GetInstance()->ResolveGraphics(pDxCommon_->GetCommandList().Get());

    ParticleEditor::GetInstance()->UpdateFrameStats();
}

// -------------------------------------------------------
// ImGui
// -------------------------------------------------------

#ifdef USE_IMGUI
namespace {
/// <summary>
/// 画質のプリセット
/// </summary>
struct QualityPreset
{
    const char *label;
    bool shadow;
    bool ssao;
    bool rtAo;
    bool bloom;
    bool cameraFade;
};
const QualityPreset kQualityPresets[] = {
    {"高", true, false, true, true, true},
    {"標準", true, true, false, true, true},
    {"軽量", false, false, false, true, true},
    {"オフ", false, false, false, false, false},
};

void ApplyQualityPreset(const QualityPreset &preset)
{
    const bool rtAo = preset.rtAo && RtAoPass::GetInstance()->IsSupported();
    ShadowMap::GetInstance()->SetEnabled(preset.shadow);
    RtAoPass::GetInstance()->SetEnabled(rtAo);
    // RT AO が使えない環境は SSAO
    SsaoRenderer::GetInstance()->SetEnabled(preset.ssao || (preset.rtAo && !rtAo));
    BloomPass::GetInstance()->SetEnabled(preset.bloom);
    CameraFade::GetSettings().enabled = preset.cameraFade;
}

/// <summary>
/// 今の設定と一致するプリセットの番号（無ければ -1）
/// </summary>
int CurrentQualityPreset()
{
    const bool rtSupported = RtAoPass::GetInstance()->IsSupported();
    for (int i = 0; i < static_cast<int>(std::size(kQualityPresets)); ++i)
    {
        const QualityPreset &p = kQualityPresets[i];
        const bool rtAo = p.rtAo && rtSupported;
        const bool ssao = p.ssao || (p.rtAo && !rtSupported);
        if (ShadowMap::GetInstance()->IsEnabled() == p.shadow && RtAoPass::GetInstance()->IsEnabled() == rtAo &&
            SsaoRenderer::GetInstance()->IsEnabled() == ssao && BloomPass::GetInstance()->IsEnabled() == p.bloom &&
            CameraFade::GetSettings().enabled == p.cameraFade)
        {
            return i;
        }
    }
    return -1;
}

void DrawQualityPresets()
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("画質");
    const int current = CurrentQualityPreset();
    for (int i = 0; i < static_cast<int>(std::size(kQualityPresets)); ++i)
    {
        ImGui::SameLine();
        if (ImGui::RadioButton(kQualityPresets[i].label, current == i))
        {
            ApplyQualityPreset(kQualityPresets[i]);
        }
    }
}

std::string StageLabel(int stageIndex)
{
    if (stageIndex == DrawSystem::kGPUParticleCompute)
    {
        return "GPU Compute";
    }
    if (stageIndex == DrawSystem::kUILayer)
    {
        return "UI";
    }
    return "Stage " + std::to_string(stageIndex);
}
} // namespace
#endif // USE_IMGUI

void DrawSystem::UpdateImGui(bool *open)
{
#ifdef USE_IMGUI
    // ウィンドウIDは保存済みレイアウトと合わせる
    if (ImGui::Begin("描画システム###DrawSystem", open, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        DrawQualityPresets();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("##drawSystemTabs"))
        {
            if (ImGui::BeginTabItem("影・ライト"))
            {
                bool shadow = ShadowMap::GetInstance()->IsEnabled();
                if (ImGui::Checkbox("影##shadowEnabled", &shadow))
                {
                    ShadowMap::GetInstance()->SetEnabled(shadow);
                }
                ImGui::Separator();
                DeferredRenderer::GetInstance()->DrawImGui();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("ポストエフェクト"))
            {
                BloomPass::GetInstance()->DrawImGui();
                ImGui::Spacing();
                VolumetricFogPass::GetInstance()->DrawImGui();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("近接フェード"))
            {
                CameraFade::DrawImGui();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("描画順"))
            {
                DrawEntryTable();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
#endif
}

void DrawSystem::DrawEntryTable()
{
#ifdef USE_IMGUI
    // 移動先にできるステージ（描画順）
    std::vector<int> stages;
    for (const auto &[idx, _] : stageOffScreens_)
    {
        stages.push_back(idx);
    }
    std::sort(stages.begin(), stages.end());
    stages.push_back(kUILayer);

    if (ImGui::BeginTable("##DrawEntries", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("表示", ImGuiTableColumnFlags_WidthFixed,
                                (std::max)(ImGui::GetFrameHeight(), ImGui::CalcTextSize("表示").x));
        ImGui::TableSetupColumn("ステージ", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (auto &entry : entries_)
        {
            ImGui::PushID(entry.name.c_str());
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Checkbox("##en", &entry.enabled);

            ImGui::TableNextColumn();
            if (entry.stageIndex == kGPUParticleCompute)
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", StageLabel(entry.stageIndex).c_str());
            }
            else
            {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##stage", StageLabel(entry.stageIndex).c_str()))
                {
                    for (int stage : stages)
                    {
                        if (ImGui::Selectable(StageLabel(stage).c_str(), stage == entry.stageIndex))
                        {
                            entry.stageIndex = stage;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (entry.enabled)
            {
                ImGui::TextUnformatted(entry.name.c_str());
            }
            else
            {
                ImGui::TextDisabled("%s", entry.name.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // 実際に描かれる順
    ImGui::SeparatorText("順番");
    stages.insert(stages.begin(), kGPUParticleCompute);
    for (int stage : stages)
    {
        ImGui::TextUnformatted(StageLabel(stage).c_str());
        ImGui::Indent();
        for (const auto &entry : entries_)
        {
            if (entry.stageIndex != stage)
            {
                continue;
            }
            if (entry.enabled)
            {
                ImGui::TextUnformatted(entry.name.c_str());
            }
            else
            {
                ImGui::TextDisabled("%s", entry.name.c_str());
            }
        }
        ImGui::Unindent();
    }

    ImGui::Spacing();
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ConfirmButton("保存", ImVec2(half, 0.0f)))
    {
        SaveConfig();
    }
    ImGui::SameLine();
    if (NeutralButton("読み込み", ImVec2(half, 0.0f)))
    {
        LoadConfig();
    }
#endif // USE_IMGUI
}

// -------------------------------------------------------
// JSON 保存 / 読み込み
// -------------------------------------------------------

void DrawSystem::SaveConfig(const std::string &fileName)
{
    auto data = std::make_unique<DataHandler>("DrawSystem", fileName);
    int count = static_cast<int>(entries_.size());
    data->Save("count", count);
    for (int i = 0; i < count; ++i)
    {
        const auto &e = entries_[i];
        std::string prefix = "entry_" + std::to_string(i) + "_";
        data->Save(prefix + "name", e.name);
        data->Save(prefix + "stage", e.stageIndex);
        data->Save(prefix + "enabled", static_cast<int>(e.enabled));
    }
    ImGuiNotification::Post("描画設定を保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

void DrawSystem::LoadConfig(const std::string &fileName)
{
    auto data = std::make_unique<DataHandler>("DrawSystem", fileName);
    int count = data->Load<int>("count", 0);
    for (int i = 0; i < count; ++i)
    {
        std::string prefix = "entry_" + std::to_string(i) + "_";
        std::string name = data->Load<std::string>(prefix + "name", "");
        int stage = data->Load<int>(prefix + "stage", 0);
        int enabled = data->Load<int>(prefix + "enabled", 1);

        for (auto &e : entries_)
        {
            if (e.name == name)
            {
                e.stageIndex = stage;
                e.enabled = static_cast<bool>(enabled);
                break;
            }
        }
    }
    ImGuiNotification::Post("描画設定を読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
}
} // namespace Hagine
