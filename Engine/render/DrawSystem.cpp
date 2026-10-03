#include "DrawSystem.h"
#ifdef USE_IMGUI
#include <edit/play/PlayModeManager.h>
#endif
#include "DirectXCommon.h"
#include "collider/CollisionManager.h"
#include "debug/profiler/CpuProfiler.h"
#include "debug/profiler/GpuProfiler.h"
#include "data/DataHandler.h"
#include "bloom/BloomPass.h"
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
#include <icon/IconsFontAwesome5.h>
#include <set>
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

        // シーン遷移は最前面（UIの上）
        pSceneManager_->DrawTransition();

        lastOffScreen->EndCompositePass();

        // ─── finalResult（フルフレーム）をバックバッファへコピー ───
        lastOffScreen->CopyFinalResultToBackBuffer();
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
/// <summary>よく使う画質の組み合わせ</summary>
struct QualityPreset
{
    const char *label;
    const char *hint;
    bool shadow;
    bool ssao;
    bool rtAo;
    bool bloom;
    bool cameraFade;
};
const QualityPreset kQualityPresets[] = {
    {ICON_FA_GEM " 高画質", "影・レイトレの遮蔽(RT AO)・ブルーム・近接フェード。レイトレが使えない環境では SSAO になる", true, false, true, true, true},
    {ICON_FA_STAR " 標準", "影・SSAO・ブルーム・近接フェード（ふだん使い）", true, true, false, true, true},
    {ICON_FA_FEATHER " 軽量", "影と遮蔽を切り、ブルームだけ残す（重いときの確認用）", false, false, false, true, true},
    {ICON_FA_EYE " 素の見た目", "影・遮蔽・ブルーム・近接フェードをすべて切る（色やモデルそのものを確かめる用）", false, false, false, false, false},
};

void ApplyQualityPreset(const QualityPreset &preset)
{
    const bool rtAo = preset.rtAo && RtAoPass::GetInstance()->IsSupported();
    ShadowMap::GetInstance()->SetEnabled(preset.shadow);
    RtAoPass::GetInstance()->SetEnabled(rtAo);
    // RT AO が使えないときは代わりに SSAO を使う
    SsaoRenderer::GetInstance()->SetEnabled(preset.ssao || (preset.rtAo && !rtAo));
    BloomPass::GetInstance()->SetEnabled(preset.bloom);
    CameraFade::GetSettings().enabled = preset.cameraFade;
}

/// <summary>今の設定がどのプリセットと同じか（どれでもなければ -1）</summary>
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

/// <summary>画質プリセットの列と、今オンになっている物の一覧</summary>
void DrawQualityPresets()
{
    ImGui::SeparatorText(ICON_FA_SLIDERS_H " 画質のプリセット");
    const int current = CurrentQualityPreset();
    const int count = static_cast<int>(std::size(kQualityPresets));
    const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (count - 1)) / static_cast<float>(count);
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        const bool selected = current == i;
        if (selected ? PrimaryButton(kQualityPresets[i].label, ImVec2(width, 0.0f)) : NeutralButton(kQualityPresets[i].label, ImVec2(width, 0.0f)))
            ApplyQualityPreset(kQualityPresets[i]);
        ImGui::SetItemTooltip("%s", kQualityPresets[i].hint);
    }
    auto flag = [](const char *label, bool on) {
        ImGui::TextColored(on ? DebugTheme::kAccentGreen : DebugTheme::kTextDim, "%s %s", on ? ICON_FA_CHECK : "-", label);
        ImGui::SameLine();
    };
    flag("影", ShadowMap::GetInstance()->IsEnabled());
    flag("SSAO", SsaoRenderer::GetInstance()->IsEnabled());
    flag("RT AO", RtAoPass::GetInstance()->IsEnabled());
    flag("ブルーム", BloomPass::GetInstance()->IsEnabled());
    flag("近接フェード", CameraFade::GetSettings().enabled);
    ImGui::NewLine();
    DimText("細かい値は下のタブと「シャドウマップ」「ポストエフェクト」の窓で。プリセットは入切だけを切り替えます");
}
} // namespace
#endif // USE_IMGUI

void DrawSystem::UpdateImGui(bool *open)
{
#ifdef USE_IMGUI
    // 表示名は日本語、ウィンドウIDは "DrawSystem" のまま（保存済みレイアウトとの互換維持）
    if (ImGui::Begin("描画システム###DrawSystem", open, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        DrawQualityPresets();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("##drawSystemTabs"))
        {
        // 影とライト（ディファードの光・遮蔽）を先に、次に画面全体の効果、最後に描画の順番
        if (ImGui::BeginTabItem(ICON_FA_LIGHTBULB " 影・ライト"))
        {
            SectionHeader("[ 影 ]", DebugTheme::kAccentYellow);
            bool shadow = ShadowMap::GetInstance()->IsEnabled();
            if (ToggleRow("影を描く", "##shadowEnabled", &shadow, DebugTheme::kAccentYellow))
                ShadowMap::GetInstance()->SetEnabled(shadow);
            DimText("影のやわらかさ・範囲などは「シャドウマップ」窓で調整します");
            ImGui::Spacing();
            SectionHeader("[ ライト・遮蔽（ディファード） ]", DebugTheme::kAccentBlue);
            DimText("たくさんの光を安く当てる描き方。SSAO と RT AO（レイトレの遮蔽）もここ");
            DeferredRenderer::GetInstance()->DrawImGui();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_MAGIC " ポストエフェクト"))
        {
            SectionHeader("[ ブルーム ]", DebugTheme::kAccentOrange);
            DimText("明るい所をにじませて光らせる。粒子の発光もここで効く");
            BloomPass::GetInstance()->DrawImGui();
            ImGui::Spacing();
            DimText("色味・ぼかし・白黒などは「ポストエフェクト」窓で調整します");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_EYE_SLASH " 近接フェード"))
        {
            DimText("カメラの近くに来た物を透かして、キャラが隠れないようにする");
            CameraFade::DrawImGui();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_LIST_OL " 描画の順番"))
        {
        SectionHeader("[ 描画エントリ ]", DebugTheme::kAccentBlue);
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::Text("登録: %zu 件", entries_.size());
        ImGui::PopStyleColor();

        if (ImGui::BeginTable("##DrawEntries", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_SizingStretchProp))
        {
            // トグルはチェックボックスより横幅を使うので列幅を広げる
            ImGui::TableSetupColumn("表示", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 1.9f);
            ImGui::TableSetupColumn("ステージ", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);

            for (auto &entry : entries_)
            {
                ImGui::PushID(entry.name.c_str());
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                // 「描いているか」の入切なのでトグルスイッチにする
                ThemedToggle("##en", &entry.enabled, DebugTheme::kAccentGreen);
                ImGui::SetItemTooltip("描画の ON / OFF");

                ImGui::TableNextColumn();
                if (entry.stageIndex == kUILayer)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgPurple);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.50f, 0.74f, 0.40f));
                    if (ImGui::SmallButton("UI Layer"))
                        entry.stageIndex = 0;
                    ImGui::PopStyleColor(2);
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgBlue);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.60f, 0.78f, 0.40f));
                    char label[32];
                    snprintf(label, sizeof(label), "Stage %d", entry.stageIndex);
                    if (ImGui::SmallButton(label))
                        entry.stageIndex = kUILayer;
                    ImGui::PopStyleColor(2);
                }
                ImGui::SetItemTooltip("クリックで UI レイヤー / ステージ を切り替え");

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(entry.name.c_str());

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        // ───────────────────────────────────────────────
        // ステージ / レイヤー管理
        //   描画エントリ(コールバック)が属するステージ(描画パス)を確認・移動する。
        //   実際の描画順: GPU Compute → Stage0 → Stage1 → ... → UI Layer
        // ───────────────────────────────────────────────
        ImGui::Spacing();
        SectionHeader("[ ステージ / 描画順 ]", DebugTheme::kAccentCyan);

        // 利用可能なステージ(レイヤー)を描画順に集める
        std::vector<int> stages;
        stages.push_back(kGPUParticleCompute);
        {
            std::vector<int> render;
            for (auto &[idx, _] : stageOffScreens_)
            {
                render.push_back(idx);
            }
            std::sort(render.begin(), render.end());
            for (int idx : render)
            {
                stages.push_back(idx);
            }
        }
        stages.push_back(kUILayer);

        auto layerLabel = [&](int idx) -> std::string {
            if (idx == kGPUParticleCompute)
                return "GPU Compute";
            if (idx == kUILayer)
                return "UI Layer";
            return "Stage " + std::to_string(idx);
        };
        auto layerColor = [&](int idx) -> ImVec4 {
            if (idx == kGPUParticleCompute)
                return DebugTheme::kAccentOrange;
            if (idx == kUILayer)
                return DebugTheme::kAccentPurple;
            return DebugTheme::kAccentBlue;
        };

        // --- 描画順の表示（各ステージに属するエントリ） ---
        ImGui::BeginChild("##drawOrder", ImVec2(0, 150), true);
        for (size_t i = 0; i < stages.size(); ++i)
        {
            int idx = stages[i];
            int count = 0;
            for (auto &e : entries_)
            {
                if (e.stageIndex == idx)
                    count++;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, layerColor(idx));
            ImGui::Text("%zu. %s", i + 1, layerLabel(idx).c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text("(%d)", count);
            ImGui::PopStyleColor();
            for (auto &e : entries_)
            {
                if (e.stageIndex != idx)
                    continue;
                ImGui::PushStyleColor(ImGuiCol_Text, e.enabled ? DebugTheme::kTextReadOnly : DebugTheme::kTextDim);
                ImGui::Text("        %s%s", e.name.c_str(), e.enabled ? "" : "  (非表示)");
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndChild();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("↑ 上から順に描画（各Stageにポストエフェクト適用、UI Layerは最前面合成）");
        ImGui::PopStyleColor();

        // --- ステージ追加（現状未対応） ---
        // OffScreen/RendererBuffer が RTV ディスクリプタを固定スロット(3,4,5)に直書きしており、
        // 複数ステージを同時に持てない（RTVヒープも8スロット固定）。
        // 動的RTV確保に対応するまで CreateStage() はUIから呼ばない。
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("※ ステージ追加は現在のエンジン構成では未対応です（OffScreenがRTVスロットを共有しているため）。");
        ImGui::PopStyleColor();

        // ───────────────────────────────────────────────
        // エントリのステージ移動（二画面 + ステージ選択ドロップダウン）
        // ───────────────────────────────────────────────
        ImGui::Spacing();
        SectionHeader("[ エントリのステージ移動 ]", DebugTheme::kAccentGreen);

        // 移動先候補は GPU Compute を除いた Stage群 + UI Layer
        std::vector<int> moveStages;
        for (int s : stages)
        {
            if (s != kGPUParticleCompute)
            {
                moveStages.push_back(s);
            }
        }

        static int leftStage = 0;         // 既定: Stage 0
        static int rightStage = kUILayer; // 既定: UI Layer
        auto clampStage = [&](int &s) {
            if (!moveStages.empty() && std::find(moveStages.begin(), moveStages.end(), s) == moveStages.end())
            {
                s = moveStages.front();
            }
        };
        clampStage(leftStage);
        clampStage(rightStage);

        // 選択状態（エントリ名をキーに保持）
        static std::set<std::string> leftSel;
        static std::set<std::string> rightSel;

        float availWidth = ImGui::GetContentRegionAvail().x;
        float btnWidth = 48.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float listWidth = (availWidth - btnWidth - spacing * 2) * 0.5f;

        // ステージ選択ドロップダウン（変更時は選択をクリア）
        auto stageCombo = [&](const char *id, int &sel, std::set<std::string> &selItems) {
            ImGui::PushID(id);
            ImGui::SetNextItemWidth(listWidth);
            if (ImGui::BeginCombo("##stage", layerLabel(sel).c_str()))
            {
                for (int s : moveStages)
                {
                    bool selected = (s == sel);
                    if (ImGui::Selectable(layerLabel(s).c_str(), selected) && s != sel)
                    {
                        sel = s;
                        selItems.clear();
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        };
        stageCombo("lc", leftStage, leftSel);
        ImGui::SameLine(listWidth + spacing + btnWidth + spacing);
        stageCombo("rc", rightStage, rightSel);

        // ステージ内のエントリをリスト表示（ダブルクリックで反対側ステージへ移動）
        auto entryList = [&](const char *id, int stage, std::set<std::string> &sel, int moveTo) {
            ImGui::BeginChild(id, ImVec2(listWidth, 200), true);
            bool any = false;
            for (auto &e : entries_)
            {
                if (e.stageIndex != stage)
                {
                    continue;
                }
                any = true;
                bool selected = sel.count(e.name) > 0;
                ImGui::PushStyleColor(ImGuiCol_Text, e.enabled ? DebugTheme::kTextReadOnly : DebugTheme::kTextDim);
                bool clicked = ImGui::Selectable(e.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick);
                ImGui::PopStyleColor();
                if (clicked)
                {
                    if (!ImGui::GetIO().KeyCtrl)
                    {
                        sel.clear();
                    }
                    if (selected)
                    {
                        sel.erase(e.name);
                    }
                    else
                    {
                        sel.insert(e.name);
                    }
                    if (ImGui::IsMouseDoubleClicked(0) && stage != moveTo)
                    {
                        e.stageIndex = moveTo;
                        sel.clear();
                    }
                }
            }
            if (!any)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("（エントリなし）");
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
        };

        // 選択中エントリをまとめて反対側ステージへ移す
        auto moveEntries = [&](int fromStage, int toStage, std::set<std::string> &sel) {
            for (auto &e : entries_)
            {
                if (e.stageIndex == fromStage && sel.count(e.name) > 0)
                {
                    e.stageIndex = toStage;
                }
            }
            sel.clear();
        };
        auto moveButton = [&](const char *label, bool enabled) -> bool {
            if (!enabled)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.30f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.30f, 0.30f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.30f, 0.30f, 0.40f));
            }
            bool pressed = ImGui::Button(label, ImVec2(btnWidth, 30)) && enabled;
            if (!enabled)
            {
                ImGui::PopStyleColor(3);
            }
            return pressed;
        };

        bool sameStage = (leftStage == rightStage);
        entryList("##leftEntries", leftStage, leftSel, rightStage);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, 60));
        if (moveButton("→##mvR", !leftSel.empty() && !sameStage))
        {
            moveEntries(leftStage, rightStage, leftSel);
        }
        ImGui::SetItemTooltip("選択を右のステージへ移動");
        if (moveButton("←##mvL", !rightSel.empty() && !sameStage))
        {
            moveEntries(rightStage, leftStage, rightSel);
        }
        ImGui::SetItemTooltip("選択を左のステージへ移動");
        ImGui::EndGroup();
        ImGui::SameLine();
        entryList("##rightEntries", rightStage, rightSel, leftStage);

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("操作: ドロップダウンで対象ステージを選択 / Ctrl+クリックで複数選択 / ダブルクリックで反対側へ移動");
        ImGui::PopStyleColor();

        ImGui::Spacing();
        SectionHeader("[ セーブ / ロード ]", DebugTheme::kAccentPurple);
        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (PrimaryButton("保存", ImVec2(bw, 0)))
        {
            SaveConfig();
        }
        ImGui::SameLine();
        if (ConfirmButton("読み込み", ImVec2(bw, 0)))
        {
            LoadConfig();
        }
        ImGui::EndTabItem();
        } // 描画の順番
        ImGui::EndTabBar();
        }
    }
    ImGui::End();
#endif
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
