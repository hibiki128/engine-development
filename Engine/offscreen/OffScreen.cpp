#include "OffScreen.h"
#include "DirectXCommon.h"
#include <Frame.h>
#include <render/ToneMapSettings.h>
#include <render/deferred/DeferredRenderer.h>
#include <shadow/ShadowMap.h>
#include <format>
#ifdef USE_IMGUI
#include "imgui.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <algorithm>
#include <icon/IconsFontAwesome5.h>
#include <vector>
#endif

namespace Hagine {
void OffScreen::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    SrvManager *pSrvManager = SrvManager::GetInstance();
    PipelineManager *psoManager = PipelineManager::GetInstance();

    renderer_.Initialize(pDxCommon_, pSrvManager, psoManager);

    // エフェクトチェーンとDirectXCommonのポインタを渡して初期化
    dataManager_.Initialize(&effectChain_, pDxCommon_);
}

void OffScreen::Draw()
{
    renderer_.Draw(effectChain_, Frame::UnscaledDeltaTime());
}

void OffScreen::DrawWithoutCopy()
{
    renderer_.DrawWithoutCopy(effectChain_, Frame::UnscaledDeltaTime());
}

void OffScreen::BeginCompositePass()
{
    renderer_.BeginCompositePass();
}

void OffScreen::EndCompositePass()
{
    renderer_.EndCompositePass();
}

void OffScreen::BlitToOffScreen(uint32_t prevFinalResultSrvIndex)
{
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu =
        SrvManager::GetInstance()->GetGPUDescriptorHandle(prevFinalResultSrvIndex);
    renderer_.BlitToOffScreen(srvGpu);
}

void OffScreen::SetProjection(Matrix4x4 projectionMatrix)
{
    // ここで受け取るのは射影行列そのもの。
    // 深度をビュー空間へ戻すのに要るのは「その逆行列」なので、ここで1回だけ求めておく。
    // （以前は射影行列をそのまま SetProjectionInverse に渡していたため、
    //   深度が正しく復元できず、深度ベースのアウトラインが出ない状態だった）
    projectionInverse_ = Inverse(projectionMatrix);

    // 深度ベースアウトライン等、射影逆行列が必要なエフェクトに反映
    const auto &slots = effectChain_.GetSlots();
    for (int i = 0; i < PostEffectChain::kMaxSlots; ++i)
    {
        if (!slots[i].occupied)
        {
            continue;
        }
        if (auto *p = effectChain_.GetParams<OutlineDepthParams>(i))
        {
            p->SetProjectionInverse(projectionInverse_);
        }
        if (auto *p = effectChain_.GetParams<DepthOfFieldParams>(i))
        {
            p->SetProjectionInverse(projectionInverse_);
        }
    }
}

void OffScreen::SetCamera(const Matrix4x4 &viewMatrix,
                          const Matrix4x4 &projectionMatrix,
                          const Vector3 &cameraPosition,
                          const Vector3 &sunDirection)
{
    SetProjection(projectionMatrix);

    // ワールド座標まで戻すには、ビューと射影をまとめた行列の逆行列が要る。
    // SSR は画面へ投影し直すので順行列のほうも要る
    viewProjection_ = viewMatrix * projectionMatrix;
    viewProjectionInverse_ = Inverse(viewProjection_);
    cameraPosition_ = cameraPosition;
    sunDirection_ = sunDirection;

    const auto &slots = effectChain_.GetSlots();
    for (int i = 0; i < PostEffectChain::kMaxSlots; ++i)
    {
        if (!slots[i].occupied)
        {
            continue;
        }
        if (auto *p = effectChain_.GetParams<HeightFogParams>(i))
        {
            p->SetCamera(viewProjectionInverse_, cameraPosition_);
            p->SetSunDirection(sunDirection_);
        }
        if (auto *p = effectChain_.GetParams<LightShaftParams>(i))
        {
            ApplyCameraToLightShaft(p);
        }
        if (auto *p = effectChain_.GetParams<SsrParams>(i))
        {
            ApplyCameraToSsr(p);
        }
        if (auto *p = effectChain_.GetParams<RtReflectionParams>(i))
        {
            ApplyCameraToRtReflection(p);
        }
    }
}

void OffScreen::ApplyCameraToSsr(SsrParams *pParams)
{
    // SSR は G-Buffer の法線を読むので、ディファードが動いているかも伝える
    pParams->SetCamera(viewProjection_, viewProjectionInverse_, cameraPosition_);
    pParams->SetNormalsAvailable(DeferredRenderer::GetInstance()->IsEnabled());
}

void OffScreen::ApplyCameraToRtReflection(RtReflectionParams *pParams)
{
    // 渡すものは SSR と同じ。交点の探し方だけが違う
    pParams->SetCamera(viewProjection_, viewProjectionInverse_, cameraPosition_);
    pParams->SetNormalsAvailable(DeferredRenderer::GetInstance()->IsEnabled());
}

void OffScreen::ApplyCameraToLightShaft(LightShaftParams *pParams)
{
    // 光の筋は「その点に光が届いているか」をシャドウマップで見るので、
    // カメラに加えてライトの行列と有効状態も要る
    ShadowMap *pShadowMap = ShadowMap::GetInstance();
    pParams->SetCamera(viewProjectionInverse_, cameraPosition_);
    pParams->SetLightDirection(sunDirection_);
    pParams->SetShadow(pShadowMap->GetLightViewProjection(), pShadowMap->IsEnabled());
}

uint32_t OffScreen::GetFinalResultSrvIndex() const
{
    return renderer_.GetFinalResultSrvIndex();
}

void OffScreen::CopyFinalResultToBackBuffer()
{
    renderer_.CopyFinalResultToBackBuffer();
}

// -------------------------------------------------------
//  エフェクト管理API（OffScreenのラッパー）
// -------------------------------------------------------

int OffScreen::AddEffect(ShaderMode mode, const std::string &name, int slotIndex)
{
    int result = effectChain_.AddEffect(mode, name, pDxCommon_, slotIndex);

    // 射影逆行列が必要なエフェクトへの即時反映
    if (result != -1)
    {
        if (auto *p = effectChain_.GetParams<OutlineDepthParams>(result))
        {
            p->SetProjectionInverse(projectionInverse_);
        }
        if (auto *p = effectChain_.GetParams<DepthOfFieldParams>(result))
        {
            p->SetProjectionInverse(projectionInverse_);
        }
        if (auto *p = effectChain_.GetParams<HeightFogParams>(result))
        {
            p->SetCamera(viewProjectionInverse_, cameraPosition_);
            p->SetSunDirection(sunDirection_);
        }
        if (auto *p = effectChain_.GetParams<LightShaftParams>(result))
        {
            ApplyCameraToLightShaft(p);
        }
        if (auto *p = effectChain_.GetParams<SsrParams>(result))
        {
            ApplyCameraToSsr(p);
        }
        if (auto *p = effectChain_.GetParams<RtReflectionParams>(result))
        {
            ApplyCameraToRtReflection(p);
        }
    }
    return result;
}

bool OffScreen::RemoveEffect(int slotIndex)
{
    return effectChain_.RemoveEffect(slotIndex);
}

int OffScreen::RemoveEffectByName(const std::string &name)
{
    return effectChain_.RemoveEffectByName(name);
}

int OffScreen::RemoveAllEffectsByName(const std::string &name)
{
    return effectChain_.RemoveAllEffectsByName(name);
}

bool OffScreen::SetEffectEnabled(int slotIndex, bool enabled)
{
    return effectChain_.SetEnabled(slotIndex, enabled);
}

bool OffScreen::MoveEffectUp(int slotIndex)
{
    return effectChain_.MoveUp(slotIndex);
}

bool OffScreen::MoveEffectDown(int slotIndex)
{
    return effectChain_.MoveDown(slotIndex);
}

void OffScreen::LoadData(const std::string &fileName)
{
    dataManager_.LoadData(fileName);
}

// -------------------------------------------------------
//  ImGui
// -------------------------------------------------------

void OffScreen::Setting()
{
#ifdef USE_IMGUI
    // 露出とトーンマップはエフェクトの並びとは別の「最後に必ず通る処理」なので、
    // チェーンの一覧より先に出しておく
    ToneMapSettings::GetInstance()->DrawImGui();
    ImGui::Separator();

    const char *shaderModeItems[] = {
        "なし", "グレイ", "ビネット", "スムース", "ガウス",
        "アウトライン(エッジ検出)", "アウトライン(深度ベース)",
        "ブラー", "シネマティック", "ディゾルブ", "ランダム", "集中線", "ピクセル化", "ブルーム", "レトロ", "衝撃波", "白黒(二値)",
        "被写界深度(DoF)",
        "アンチエイリアス(FXAA)", "カラーグレーディング", "色収差", "フィルムグレイン", "レンズ歪み",
        "フォグ(距離＋高さ)", "光の筋(レイマーチ)", "画面内反射(SSR)", "RT反射", "打撃インパクト"};

    // 各エフェクトが何をするかの一言説明（shaderModeItems と同じ並び＝ShaderMode順）。
    // 「効果の中身が分からない」対策として追加/選択UIに表示する。
    const char *shaderModeDescs[] = {
        "エフェクトなし（そのまま出力）",
        "全体を灰色にする（グレースケール）",
        "画面の四隅を暗く落とす（ビネット）",
        "画面全体をなめらかにぼかす",
        "ガウスぼかし（きれいなぼかし）",
        "色の変化から輪郭線を抽出して縁取る",
        "奥行き（深度）の差から輪郭線を描く",
        "中心から放射状にブレさせる（集中ぼかし）",
        "コントラスト/彩度/明度を整える映画風",
        "ノイズ画像でだんだん溶かして消す（画像を設定可）",
        "画面全体にノイズ（ざらつき）を乗せる",
        "中心へ向かう集中線を描く",
        "モザイク状にピクセル化する",
        "明るい部分を光らせて滲ませる（ブルーム）",
        "レトロ風（走査線など）に加工する",
        "衝撃波のように画面を歪ませる",
        "完全な白黒（明度で白か黒に二値化）",
        "ピント面から外れた場所をぼかす（被写界深度）",
        "輪郭のギザギザを馴染ませる（最後のほうに置く）",
        "画面全体の色味を作り込む（色温度・暗部/中間/明部）",
        "画面の端で赤青がずれる、レンズ越しの生々しさ",
        "フィルムのような細かいざらつきを乗せる",
        "広角レンズのように画面を曲げる（周辺減光つき）",
        "遠くを霞ませて奥行きを出す。低地に霧をためることもできる",
        "物陰から光芒が伸びる（木漏れ日・窓から差す光）。シャドウマップが要る",
        "床や水面に周囲が映り込む。ディファードが要る（画面外の物は映らない）",
        "レイトレーシングで映り込ませる。画面外を向いた反射でも空が正しく映る",
        "当たった瞬間の衝撃波の歪み・集中ブラー・色収差・フラッシュ・白黒の1コマ（演出側から操作する）",
    };
    static_assert(IM_ARRAYSIZE(shaderModeItems) == static_cast<int>(ShaderMode::Count),
                  "shaderModeItems は ShaderMode::Count と同数にすること");
    static_assert(IM_ARRAYSIZE(shaderModeDescs) == static_cast<int>(ShaderMode::Count),
                  "shaderModeDescs は ShaderMode::Count と同数にすること");

    // 種類ごとのまとまり（追加メニューの見出しと色に使う）
    struct EffectCategory
    {
        const char *name;
        ImVec4 color;
        std::vector<ShaderMode> modes;
    };
    static const EffectCategory kCategories[] = {
        {ICON_FA_PALETTE " 色・トーン", DebugTheme::kAccentYellow,
         {ShaderMode::Gray, ShaderMode::Cinematic, ShaderMode::Monochrome, ShaderMode::ColorGrading, ShaderMode::Retro}},
        {ICON_FA_TINT " ぼかし", DebugTheme::kAccentBlue,
         {ShaderMode::Smooth, ShaderMode::Gauss, ShaderMode::Blur, ShaderMode::DepthOfField}},
        {ICON_FA_PEN " 輪郭・アンチエイリアス", DebugTheme::kAccentCyan,
         {ShaderMode::Outline, ShaderMode::Depth, ShaderMode::Fxaa}},
        {ICON_FA_FILM " 画面の加工", DebugTheme::kAccentPurple,
         {ShaderMode::Vignette, ShaderMode::Random, ShaderMode::FocusLine, ShaderMode::Pixelate, ShaderMode::FilmGrain, ShaderMode::Dissolve}},
        {ICON_FA_SUN " 光・レンズ", DebugTheme::kAccentOrange,
         {ShaderMode::Bloom, ShaderMode::ChromaticAberration, ShaderMode::LensDistortion, ShaderMode::Shockwave, ShaderMode::Impact}},
        {ICON_FA_CLOUD " 空間・映り込み", DebugTheme::kAccentGreen,
         {ShaderMode::HeightFog, ShaderMode::LightShaft, ShaderMode::Ssr, ShaderMode::RtReflection}},
    };
    auto categoryColor = [&](ShaderMode mode) {
        for (const EffectCategory &category : kCategories)
        {
            if (std::find(category.modes.begin(), category.modes.end(), mode) != category.modes.end())
                return category.color;
        }
        return DebugTheme::kTextDim;
    };

    const auto &slots = effectChain_.GetSlots();
    auto findSlot = [&](ShaderMode mode) {
        for (int i = 0; i < PostEffectChain::kMaxSlots; ++i)
        {
            if (slots[i].occupied && slots[i].params && slots[i].params->GetMode() == mode)
                return i;
        }
        return -1;
    };

    // ── 見出し: 使っている数・追加・まとめて操作 ──
    const int freeSlots = effectChain_.GetFreeSlotCount();
    SectionHeader("[ ポストエフェクト ]", DebugTheme::kAccentBlue);
    ImGui::BeginDisabled(freeSlots <= 0);
    if (PrimaryButton(ICON_FA_PLUS " エフェクトを追加"))
    {
        ImGui::OpenPopup("##addPostEffect");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    StatusBadge(std::format("{} / {} 使用中", PostEffectChain::kMaxSlots - freeSlots, PostEffectChain::kMaxSlots).c_str(),
                freeSlots > 0 ? DebugTheme::kAccentGreen : DebugTheme::kAccentOrange);
    ImGui::SameLine();
    if (NeutralButton("全部OFF"))
    {
        for (int i = 0; i < PostEffectChain::kMaxSlots; ++i)
        {
            if (slots[i].occupied)
                effectChain_.SetEnabled(i, false);
        }
        ImGuiNotification::Post("全エフェクトを無効化しました", {0.82f, 0.58f, 0.36f, 1.0f});
    }
    ImGui::SameLine();
    if (DangerButton("全部削除"))
    {
        for (int i = PostEffectChain::kMaxSlots - 1; i >= 0; --i)
        {
            effectChain_.RemoveEffect(i);
        }
        ImGuiNotification::Post("全エフェクトを削除しました", {0.82f, 0.58f, 0.36f, 1.0f});
    }

    // ── 追加メニュー（種類ごと・検索付き。もう入っている物は印と「外す」）──
    if (ImGui::BeginPopup("##addPostEffect"))
    {
        static std::string search;
        if (ImGui::IsWindowAppearing())
        {
            search.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputTextWithHint("##fxSearch", ICON_FA_SEARCH " 名前や説明で探す（例: ぼかし・光）", &search);
        for (const EffectCategory &category : kCategories)
        {
            bool headerShown = false;
            for (ShaderMode mode : category.modes)
            {
                const int m = static_cast<int>(mode);
                if (!search.empty() && std::string(shaderModeItems[m]).find(search) == std::string::npos &&
                    std::string(shaderModeDescs[m]).find(search) == std::string::npos)
                {
                    continue;
                }
                if (!headerShown)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, category.color);
                    ImGui::SeparatorText(category.name);
                    ImGui::PopStyleColor();
                    headerShown = true;
                }
                const int existing = findSlot(mode);
                if (ImGui::MenuItem(shaderModeItems[m], existing >= 0 ? "使用中" : nullptr, existing >= 0))
                {
                    if (existing < 0)
                    {
                        if (AddEffect(mode, shaderModeItems[m], -1) == -1)
                            ImGuiNotification::Post("追加できませんでした（スロットが満杯）", {0.85f, 0.42f, 0.42f, 1.0f});
                        else
                            ImGuiNotification::Post(std::format("エフェクトを追加しました: {}", shaderModeItems[m]), {0.45f, 0.68f, 0.52f, 1.0f});
                    }
                    else
                    {
                        effectChain_.SetEnabled(existing, true);
                    }
                }
                ImGui::SetItemTooltip("%s", shaderModeDescs[m]);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    // ── 使っているエフェクト（上から順にかかる。行をドラッグで並べ替え）──
    int dragFrom = -1; // ドラッグ元スロット
    int dragTo = -1;   // ドロップ先スロット（ループ後にまとめて入れ替える）
    int pendingRemove = -1;
    int shown = 0;
    for (int i = 0; i < PostEffectChain::kMaxSlots; ++i)
    {
        if (!slots[i].occupied || !slots[i].params)
            continue;
        ++shown;
        const auto &slot = slots[i];
        const ShaderMode mode = slot.params->GetMode();
        const ImVec4 color = categoryColor(mode);
        ImGui::PushID(i);

        // 有効トグル
        bool enabled = slot.enabled;
        if (ThemedToggle("##en", &enabled, color))
            effectChain_.SetEnabled(i, enabled);
        ImGui::SetItemTooltip("このエフェクトの有効 / 無効");
        ImGui::SameLine();

        // 見出し（開くとパラメータ）。ドラッグで並べ替え
        ImVec4 headerColor = color;
        headerColor.w = slot.enabled ? 0.28f : 0.10f;
        ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
        ImGui::PushStyleColor(ImGuiCol_Text, slot.enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const std::string label = std::format("{}. {}###fxslot{}", shown, shaderModeItems[static_cast<int>(mode)], i);
        const bool open = ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip("%s\nドラッグで順番を入れ替え（上から順にかかります）", shaderModeDescs[static_cast<int>(mode)]);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            ImGui::SetDragDropPayload("FX_SLOT", &i, sizeof(int));
            ImGui::Text("移動: %s", shaderModeItems[static_cast<int>(mode)]);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("FX_SLOT"))
            {
                dragFrom = *static_cast<const int *>(payload->Data);
                dragTo = i;
            }
            ImGui::EndDragDropTarget();
        }

        // 右端: 上へ / 下へ / 外す
        const float buttonSize = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonSize * 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f);
        {
            ScopedButtonColors ghost(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
            if (ImGui::Button(ICON_FA_ARROW_UP, ImVec2(buttonSize, buttonSize)) && i > 0)
                effectChain_.MoveUp(i);
            ImGui::SetItemTooltip("ひとつ前にかける");
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ARROW_DOWN, ImVec2(buttonSize, buttonSize)) && i < PostEffectChain::kMaxSlots - 1)
                effectChain_.MoveDown(i);
            ImGui::SetItemTooltip("ひとつ後にかける");
        }
        ImGui::SameLine();
        {
            ScopedButtonColors danger(DebugTheme::kButtonGhost, DebugTheme::kButtonDangerHover);
            if (ImGui::Button(ICON_FA_TIMES, ImVec2(buttonSize, buttonSize)))
                pendingRemove = i;
            ImGui::SetItemTooltip("外す");
        }

        if (open)
        {
            ImGui::Indent();
            DimText(shaderModeDescs[static_cast<int>(mode)]);
            ImGui::BeginDisabled(!slot.enabled);
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
            slot.params->DrawUI();
            ImGui::PopItemWidth();
            ImGui::EndDisabled();
            ImGui::Unindent();
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    if (shown == 0)
    {
        DimText("エフェクトはありません。「＋ エフェクトを追加」から足してください");
    }
    // ドロップ確定後にまとめて入れ替える（ループ中のスロット変更を避ける）
    if (dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo)
    {
        effectChain_.SwapSlots(dragFrom, dragTo);
    }
    if (pendingRemove >= 0)
    {
        const std::string removedName = shaderModeItems[static_cast<int>(slots[pendingRemove].params->GetMode())];
        effectChain_.RemoveEffect(pendingRemove);
        ImGuiNotification::Post("エフェクトを外しました: " + removedName, {0.82f, 0.58f, 0.36f, 1.0f});
    }

    ImGui::Spacing();

    // ── セーブ / ロード ──
    SectionHeader("[ セーブ / ロード ]", DebugTheme::kAccentPurple);
    static char saveFileName[256] = "OffScreenData";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ofsfile", "ファイル名", saveFileName, sizeof(saveFileName));
    ImGui::Spacing();

    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (PrimaryButton(ICON_FA_SAVE " セーブ", ImVec2(bw, 0)))
    {
        dataManager_.SaveData(std::string(saveFileName));
        ImGuiNotification::Post(std::format("オフスクリーン設定を保存しました: {}", saveFileName), {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_UPLOAD " ロード", ImVec2(bw, 0)))
    {
        dataManager_.LoadData(std::string(saveFileName));
        ImGuiNotification::Post(std::format("オフスクリーン設定を読み込みました: {}", saveFileName), {0.42f, 0.66f, 0.68f, 1.0f});
    }
#endif
}
} // namespace Hagine
