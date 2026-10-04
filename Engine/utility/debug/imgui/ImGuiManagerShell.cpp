#include "ImGuiManager.h"
#ifdef USE_IMGUI
// エディタの「枠組み」部分。個々の窓の中身ではなく、窓を探す・切り替える・状態を見るための部品をまとめる。
//   ・ウィンドウ一覧（メニュー・コマンドパレット・ワークスペースが共通で使う）
//   ・ワークスペース（作業ごとの窓の組み合わせを一発で切り替える）
//   ・ステータスバー（シーン・再生状態・選択・最新の通知・FPS を常に見せる）
//   ・通知の履歴窓 / 外観の設定窓 / コマンドパレット（Ctrl+K）
#include "DebugUIHelper.h"
#include "ImGuiNotification.h"
#include "ImGuizmoManager.h"
#include <algorithm>
#include <debug/capture/CaptureManager.h>
#include <edit/play/PlayModeManager.h>
#include <edit/undo/UndoRedoManager.h>
#include <format>
#include <icon/IconsFontAwesome5.h>
#include <render/SceneViewRenderer.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <scene/SceneManager.h>
#include <scene/SceneRegistry.h>
#include <scene/SceneSerializer.h>

namespace Hagine {

void ImGuiManager::BuildWindowRegistry()
{
    // 並び順がそのままメニューの並びになる（見出しが変わるところで区切り線が入る）
    windowRegistry_ = {
        {"scene", ICON_FA_BOOK_OPEN, "シーン設定", "シーン・オブジェクト", "シーン全体の設定（カメラ・背景など）を編集します", &showSceneView_},
        {"transition", ICON_FA_DOOR_OPEN, "シーン遷移", "シーン・オブジェクト", "シーンを切り替えるときの演出（フェード・ワイプ・アイリス・六角形…）を作り、その場で試し、どの切り替えにどれを使うか決めます", &showTransitionView_},
        {"hierarchy", ICON_FA_PROJECT_DIAGRAM, "オブジェクトマネージャ (階層)", "シーン・オブジェクト", "シーン内オブジェクトの一覧・検索・選択・親子付けを操作します", &showHierarchyView_},
        {"selection", ICON_FA_INFO_CIRCLE, "インスペクタ", "シーン・オブジェクト", "選択中の物の詳細を編集します。複数選択ならまとめて編集。ピン留めで表示を固定できます", &showInspectorView_},
        {"cameraView", ICON_FA_VIDEO, "カメラビュー", "シーン・オブジェクト", "好きなカメラから見たシーンを別の窓に映します（窓の中の＋で最大4つまで）", SceneViewRenderer::GetInstance()->GetFirstViewOpenFlag()},
        {"debugCamera", ICON_FA_STREET_VIEW, "デバッグカメラ", "シーン・オブジェクト", "エディタで自由に飛び回るカメラ（F3）の切り替え・位置と向き・動かし方", &showDebugCameraView_},
        {"inspector", ICON_FA_CUBE, "シーンのオブジェクト設定", "シーン・オブジェクト", "シーンが持つキャラクターなど（プレイヤー・敵・演出）の調整項目", &showObjectView_},
        {"gizmo", ICON_FA_ARROWS_ALT, "ギズモ (トランスフォーム)", "シーン・オブジェクト", "移動/回転/拡縮の操作と、操作対象の種類フィルタ", &showGizmoView_},
        {"placement", ICON_FA_TH, "配置ツール", "シーン・オブジェクト", "選択中のオブジェクトを直線・格子・円・ばらまきで並べて複製します（置く場所を下描きしてから作れる）", &showPlacementToolView_},
        {"collider", ICON_FA_SHAPES, "コライダー", "シーン・オブジェクト", "当たり判定の確認・調整とタグ管理", &showColliderTagManagerView_},

        {"assets", ICON_FA_IMAGES, "アセットブラウザ", "アセット・UI", "画像・モデル・プレハブ・サウンド・JSON を探して、ドラッグで割り当て・配置します", &showAssetBrowserView_},
        {"sprites", ICON_FA_SQUARE, "スプライトマネージャ", "アセット・UI", "2Dスプライトの一覧・編集と、文字スプライトの作成", &showSpriteManagerView_},
        {"uiEditor", ICON_FA_PLAY_CIRCLE, "UIエディタ", "アセット・UI", "スプライトのグループ化と、名前付きイージング(トゥイーン)の作成・再生", &showUIEditorView_},

        {"motion", ICON_FA_CODE_BRANCH, "モーションエディター", "アニメーション・AI", "オブジェクトのモーション（アニメーション）を編集します", &showMotionEditorView_},
        {"timeline", ICON_FA_FILM, "タイムライン", "アニメーション・AI", "カメラ・オブジェクト・音・パーティクルを1本の時間軸に並べて演出を作ります", &showTimelineView_},
        {"behaviorTree", ICON_FA_SITEMAP, "ビヘイビアツリーエディタ", "アニメーション・AI", "キャラクターのAI（ビヘイビアツリー）をノードで組み、その場で動かして確かめます", &showBehaviorTreeView_},
        {"animStateMachine", ICON_FA_STREAM, "アニメーションステートマシン", "アニメーション・AI", "待機→走り→ジャンプのようなアニメーションの切り替えを、ステートと遷移の線で組みます", &showAnimStateMachineView_},

        {"particle", ICON_FA_STAR, "パーティクル設定", "パーティクル", "パーティクル/フィールドの設定と、シーンへの配置", &showParticleView_},
        {"particlePreview", ICON_FA_IMAGE, "パーティクルプレビュー", "パーティクル", "GPUパーティクルを単体でプレビューします", &showParticlePreviewView_},

        {"camera", ICON_FA_VIDEO, "カメラ", "レンダリング", "登録カメラの一覧・切り替え・位置/画角の設定", &showCameraView_},
        {"light", ICON_FA_LIGHTBULB, "ライト", "レンダリング", "ライティング（平行光・環境光など）の設定", &showLightView_},
        {"postEffect", ICON_FA_STAR_OF_DAVID, "オフスクリーン (ポストエフェクト)", "レンダリング", "ポストエフェクト（ブラー・色調・白黒など）の設定", &showOfScreenView_},
        {"shadow", ICON_FA_ADJUST, "シャドウマップ", "レンダリング", "影の描画設定・デバッグ表示", &showShadowMapView_},
        {"drawSystem", ICON_FA_LAYER_GROUP, "描画システム", "レンダリング", "描画ステージ/順序などレンダリング全体の設定", &showDrawSystemView_},
        {"shader", ICON_FA_CODE, "シェーダー", "レンダリング", "shaders/ 配下のHLSLを構文色付きで閲覧・編集します", &showShaderEditorView_},

        {"audio", ICON_FA_BULLHORN, "オーディオ", "サウンド", "再生中サウンドの確認・音量調整", &showAudioManagerView_},
        {"music", ICON_FA_MUSIC, "音楽エディタ", "サウンド", "鍵盤を弾く・ピアノロールで打ち込む・音色を作る・wav を加工して書き出す", &showMusicEditorView_},

        {"gameParam", ICON_FA_SLIDERS_H, "ゲームパラメータ", "調整・デバッグ", "コードに登録したパラメータを実行中に調整・保存・仕分けします", &showGameParamView_},
        {"stats", ICON_FA_DATABASE, "統計 (FPS/プロファイラ/ログ)", "調整・デバッグ", "FPS・処理時間・ログ履歴を表示します", &showFPSView_},
        {"inputActions", ICON_FA_GAMEPAD, "入力（キーコンフィグ）", "調整・デバッグ", "ゲームが登録した行動（ジャンプ・移動など）へのキー・パッドの割り当てを付け替えて保存し、押している様子も確かめます", &showInputActionView_},
        {"debugLines", ICON_FA_PENCIL_RULER, "デバッグ線", "調整・デバッグ", "シーンに出ている線（グリッド・コライダー・ライト・選択の枠など）を種類ごとに確かめて、出す/隠すを切り替えます", &showDebugLineView_},
        {"console", ICON_FA_TERMINAL, "コンソール", "調整・デバッグ", "実行中にコマンドを打って、値の確認・書き換え・シーン切り替えなどを行います", &showConsoleView_},
        {"capture", ICON_FA_CAMERA, "キャプチャ", "調整・デバッグ", "スクリーンショットと連番録画。提出用の画像・動画素材づくりに使います", &showCaptureView_},
        {"undoHistory", ICON_FA_HISTORY, "操作の履歴", "調整・デバッグ", "Undo の履歴を一覧し、押した行の時点まで一気に戻す・やり直します", &showUndoHistoryView_},
        {"notifications", ICON_FA_BELL, "通知の履歴", "調整・デバッグ", "これまでに出た通知を時刻付きで見返します（ステータスバーの通知をクリックしても開きます）", &showNotificationView_},

        {"colorPalette", ICON_FA_TINT, "カラーパレット", "エディタ", "色を貯めておき、各所の色の欄へドラッグで渡します", &showColorPaletteView_},
        {"appearance", ICON_FA_PALETTE, "外観", "エディタ", "配色・アクセント色・UIの大きさ・角の丸みを変えます", &showAppearanceView_},
    };
}

void ImGuiManager::BuildWorkspaces()
{
    // シーンのクリック対象の既定（作業に関係ない種類を掴まないように絞る）
    constexpr uint32_t kPickObject = 1u << static_cast<uint32_t>(GizmoCategory::Object);
    constexpr uint32_t kPickSprite = 1u << static_cast<uint32_t>(GizmoCategory::Sprite);
    constexpr uint32_t kPickParticle = 1u << static_cast<uint32_t>(GizmoCategory::Particle);
    constexpr uint32_t kPickLight = 1u << static_cast<uint32_t>(GizmoCategory::Light);
    constexpr uint32_t kPick3D = kPickObject | kPickParticle | kPickLight;

    workspaces_ = {
        {"シーン編集", ICON_FA_CUBES, "オブジェクトを置いて整える。階層・インスペクタ・ギズモ・アセット・ライト",
         {"scene", "selection", "hierarchy", "gizmo", "assets", "light", "camera"}, kPickObject | kPickLight},
        {"エフェクト", ICON_FA_FIRE, "パーティクルとポストエフェクトを作る",
         {"particle", "particlePreview", "postEffect", "hierarchy", "selection", "gameParam"}, kPickParticle},
        {"演出", ICON_FA_FILM, "カメラワークとモーションで見せ場を作る",
         {"timeline", "camera", "motion", "audio", "hierarchy", "selection", "inspector", "transition"}, kPickObject | kPickLight},
        {"UI", ICON_FA_SQUARE, "スプライトとUIアニメーションを作る",
         {"sprites", "uiEditor", "assets"}, kPickSprite},
        {"サウンド", ICON_FA_MUSIC, "音楽を作り、鳴り方を確かめる",
         {"music", "audio"}, kPick3D},
        {"調整・デバッグ", ICON_FA_SLIDERS_H, "ゲームパラメータを詰め、処理の重さや当たり判定を確かめる",
         {"gameParam", "stats", "console", "collider", "debugLines", "capture", "notifications"}, kPick3D},
        {"AI調整", ICON_FA_SITEMAP, "ビヘイビアツリーを組み、敵の作戦（オブジェクト設定の AI タブ）とパラメータを見ながら詰める",
         {"behaviorTree", "inspector", "gameParam", "stats"}, kPick3D},
        {"ゲーム画面だけ", ICON_FA_DESKTOP, "窓を全部閉じてシーンだけを見る", {}, kPick3D},
    };
    workspacePickMasks_.clear();
    for (const EditorWorkspace &workspace : workspaces_)
    {
        workspacePickMasks_.push_back(workspace.pickMask);
    }
}

namespace {
// 表示メニューの分類の並びとアイコン（ウィンドウ表の分類名と同じにする）
struct WindowCategoryInfo
{
    const char *name;
    const char *icon;
};
constexpr WindowCategoryInfo kWindowCategories[] = {
    {"シーン・オブジェクト", ICON_FA_CUBES},
    {"アセット・UI", ICON_FA_IMAGES},
    {"アニメーション・AI", ICON_FA_SITEMAP},
    {"パーティクル", ICON_FA_STAR},
    {"レンダリング", ICON_FA_SUN},
    {"サウンド", ICON_FA_MUSIC},
    {"調整・デバッグ", ICON_FA_BUG},
    {"エディタ", ICON_FA_COG},
};

// 大文字小文字を気にせず含まれるか（日本語はそのまま比べる）
bool ContainsText(const std::string &text, const std::string &needle)
{
    auto lower = [](std::string v) {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return v;
    };
    return lower(text).find(lower(needle)) != std::string::npos;
}
} // namespace

void ImGuiManager::DrawWindowToggle(const EditorWindowEntry &entry, bool showCategory)
{
    // チェック付きのトグル行。クリックしてもメニューは閉じない（続けて何枚も開ける）
    const std::string label = std::string(entry.icon) + " " + entry.label;
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    if (ImGui::MenuItem(label.c_str(), showCategory ? entry.category : nullptr, *entry.flag))
    {
        *entry.flag = !*entry.flag;
    }
    ImGui::PopItemFlag();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", entry.tip);
    }
}

void ImGuiManager::DrawWindowMenuItems()
{
    // 分類ごとのサブメニュー。見出しに「開いている数 / 全部」を出して、どこに何が開いているか分かるようにする
    for (const WindowCategoryInfo &category : kWindowCategories)
    {
        int total = 0;
        int open = 0;
        for (const EditorWindowEntry &entry : windowRegistry_)
        {
            if (std::string(entry.category) != category.name)
                continue;
            ++total;
            open += *entry.flag ? 1 : 0;
        }
        if (total == 0)
            continue;

        // 数が変わってもサブメニューが閉じないよう、ID は分類名で固定する
        const std::string label = std::format("{} {}   {}/{}###viewCat_{}", category.icon, category.name, open, total, category.name);
        ImGui::PushStyleColor(ImGuiCol_Text, open > 0 ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : DebugTheme::kTextReadOnly);
        const bool menuOpen = ImGui::BeginMenu(label.c_str());
        ImGui::PopStyleColor();
        if (!menuOpen)
            continue;

        for (const EditorWindowEntry &entry : windowRegistry_)
        {
            if (std::string(entry.category) == category.name)
                DrawWindowToggle(entry, false);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " この分類を全部開く"))
        {
            for (const EditorWindowEntry &entry : windowRegistry_)
                if (std::string(entry.category) == category.name)
                    *entry.flag = true;
        }
        if (ImGui::MenuItem(ICON_FA_FOLDER " この分類を全部閉じる", nullptr, false, open > 0))
        {
            for (const EditorWindowEntry &entry : windowRegistry_)
                if (std::string(entry.category) == category.name)
                    *entry.flag = false;
        }
        ImGui::EndMenu();
    }
}

void ImGuiManager::DrawViewMenu()
{
    // ---- 窓を探す ----
    if (ImGui::IsWindowAppearing())
    {
        viewMenuSearch_.clear();
    }
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##viewSearch", ICON_FA_SEARCH " 窓を探す（名前・説明）", &viewMenuSearch_);
    if (!viewMenuSearch_.empty())
    {
        // 探しているときは分類をまたいで一覧にする（右に分類）
        ImGui::Separator();
        int hits = 0;
        for (const EditorWindowEntry &entry : windowRegistry_)
        {
            if (!ContainsText(entry.label, viewMenuSearch_) && !ContainsText(entry.tip, viewMenuSearch_) &&
                !ContainsText(entry.category, viewMenuSearch_))
                continue;
            ++hits;
            DrawWindowToggle(entry, true);
        }
        if (hits == 0)
            DimText("見つかりません");
        return;
    }

    // ---- ウィンドウ ----
    ImGui::SeparatorText("ウィンドウ");
    DrawWindowMenuItems();

    int openCount = 0;
    for (const EditorWindowEntry &entry : windowRegistry_)
        openCount += *entry.flag ? 1 : 0;
    const std::string openLabel = std::format("{} 開いている窓 ({})", ICON_FA_WINDOW_RESTORE, openCount);
    if (ImGui::BeginMenu(openLabel.c_str(), openCount > 0))
    {
        // 押すとその窓を閉じる
        for (const EditorWindowEntry &entry : windowRegistry_)
        {
            if (*entry.flag)
                DrawWindowToggle(entry, true);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_TIMES " すべて閉じる"))
        {
            for (const EditorWindowEntry &entry : windowRegistry_)
                *entry.flag = false;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(ICON_FA_TH_LARGE " ワークスペース"))
    {
        DrawWorkspaceMenuItems();
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(ICON_FA_SEARCH " コマンドパレット", "Ctrl+K"))
    {
        commandPalette_->Open();
    }

    // ---- 画面 ----
    ImGui::SeparatorText("画面");
    DrawGridMenu();
    DrawScreenMenuItems();
}

void ImGuiManager::DrawWorkspaceMenuItems()
{
    for (int i = 0; i < static_cast<int>(workspaces_.size()); ++i)
    {
        const EditorWorkspace &workspace = workspaces_[i];
        const std::string label = std::string(workspace.icon) + " " + workspace.name;
        if (ImGui::MenuItem(label.c_str(), nullptr, currentWorkspace_ == i))
        {
            ApplyWorkspace(i);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", workspace.description);
        }
    }
}

void ImGuiManager::ApplyWorkspace(int index)
{
    if (index < 0 || index >= static_cast<int>(workspaces_.size()))
    {
        return;
    }
    const EditorWorkspace &workspace = workspaces_[index];
    for (const EditorWindowEntry &entry : windowRegistry_)
    {
        const bool open = std::find_if(workspace.windows.begin(), workspace.windows.end(),
                                       [&](const char *id) { return std::string(id) == entry.id; }) != workspace.windows.end();
        *entry.flag = open;
    }

    // シーンのクリック対象: 今の状態を元のワークスペースに覚えさせてから、切り替え先のものにする
    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    if (currentWorkspace_ >= 0 && currentWorkspace_ < static_cast<int>(workspacePickMasks_.size()))
    {
        workspacePickMasks_[currentWorkspace_] = gizmo->GetCategoryMask();
    }
    if (index < static_cast<int>(workspacePickMasks_.size()))
    {
        gizmo->SetCategoryMask(workspacePickMasks_[index]);
    }
    currentWorkspace_ = index;
    ImGuiNotification::Post(std::string("ワークスペース: ") + workspace.name, {0.72f, 0.58f, 0.90f, 1.0f});
}

void ImGuiManager::DrawStatusBar()
{
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
    if (ImGui::BeginViewportSideBar("##EditorStatusBar", viewport, ImGuiDir_Down, height, flags))
    {
        if (ImGui::BeginMenuBar())
        {
            auto separator = [] {
                ImGui::SameLine(0.0f, 10.0f);
                ImGui::TextDisabled("|");
                ImGui::SameLine(0.0f, 10.0f);
            };

            // ---- シーン名 ----
            ImGui::TextColored(ImVec4(0.40f, 0.80f, 0.80f, 1.0f), ICON_FA_GLOBE);
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextUnformatted(SceneManager::GetInstance()->GetCurrentSceneName().c_str());
            separator();

            // ---- 再生状態 ----
            {
                const PlayModeManager::State state = PlayModeManager::GetInstance()->GetState();
                const char *icon = ICON_FA_STOP;
                const char *text = "停止中";
                ImVec4 color(0.62f, 0.64f, 0.68f, 1.0f);
                if (state == PlayModeManager::State::Playing)
                {
                    icon = ICON_FA_PLAY;
                    text = "再生中";
                    color = ImVec4(0.45f, 0.80f, 0.52f, 1.0f);
                }
                else if (state == PlayModeManager::State::Paused)
                {
                    icon = ICON_FA_PAUSE;
                    text = "一時停止";
                    color = ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
                }
                ImGui::TextColored(color, "%s %s", icon, text);
            }
            separator();

            // ---- 選択 ----
            {
                const auto &selected = pImGuizmoManager_ ? pImGuizmoManager_->GetSelectedNames() : std::unordered_set<std::string>{};
                if (selected.empty())
                {
                    ImGui::TextDisabled(ICON_FA_MOUSE_POINTER " 選択なし");
                }
                else
                {
                    // 表示は名前順の先頭（毎フレーム入れ替わらないように）
                    std::string first = *std::min_element(selected.begin(), selected.end());
                    if (selected.size() > 1)
                    {
                        first += std::format(" ほか{}件", selected.size() - 1);
                    }
                    ImGui::Text(ICON_FA_MOUSE_POINTER " %s", first.c_str());
                }
            }
            separator();

            // ---- ワークスペース（クリックで切り替え）----
            {
                const char *name = (currentWorkspace_ >= 0 && currentWorkspace_ < static_cast<int>(workspaces_.size()))
                                       ? workspaces_[currentWorkspace_].name
                                       : "ワークスペース";
                if (ImGui::BeginMenu((std::string(ICON_FA_TH_LARGE " ") + name + "##ws").c_str()))
                {
                    DrawWorkspaceMenuItems();
                    ImGui::EndMenu();
                }
            }

            // ---- Undo ----
            if (UndoRedoManager::GetInstance()->CanUndo())
            {
                separator();
                ImGui::TextDisabled(ICON_FA_UNDO " %s", UndoRedoManager::GetInstance()->GetUndoLabel().c_str());
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("クリックで操作の履歴を開く");
                }
                if (ImGui::IsItemClicked())
                {
                    showUndoHistoryView_ = true;
                }
            }

            // ---- 右側: 最新の通知・フレーム時間・コマンドパレット ----
            const ImGuiIO &io = ImGui::GetIO();
            const float fps = io.Framerate;
            const std::string frameText = std::format("{:.0f} FPS  {:.1f} ms", fps, fps > 0.0f ? 1000.0f / fps : 0.0f);
            const char *paletteLabel = ICON_FA_SEARCH " Ctrl+K";

            const auto &history = ImGuiNotification::GetHistory();
            std::string latest;
            ImVec4 latestColor(0.62f, 0.64f, 0.68f, 1.0f);
            if (!history.empty())
            {
                latest = ICON_FA_BELL " " + history.back().time + "  " + history.back().message;
                latestColor = ImVec4(history.back().color.x, history.back().color.y, history.back().color.z, 1.0f);
            }

            const ImGuiStyle &style = ImGui::GetStyle();
            const float paletteWidth = ImGui::CalcTextSize(paletteLabel).x + style.FramePadding.x * 2.0f;
            const float frameWidth = ImGui::CalcTextSize(frameText.c_str()).x;
            constexpr float kSparkWidth = 72.0f; // フレーム時間の小さなグラフ
            const float rightStart = ImGui::GetWindowWidth() - paletteWidth - frameWidth - kSparkWidth - style.WindowPadding.x - 48.0f;
            const float cursorX = ImGui::GetCursorPosX();

            // 通知は左の項目と右の項目の間に入るだけ出す（はみ出す分は切る）
            if (!latest.empty() && rightStart - cursorX > 120.0f)
            {
                ImGui::SameLine(0.0f, 20.0f);
                const float available = rightStart - ImGui::GetCursorPosX() - 16.0f;
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::PushID("##latestNotification");
                if (ImGui::InvisibleButton("##latest", ImVec2(std::max(available, 1.0f), ImGui::GetTextLineHeight())))
                {
                    showNotificationView_ = !showNotificationView_;
                }
                const bool hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                if (hovered)
                {
                    ImGui::SetTooltip("クリックで通知の履歴を開く");
                }
                ImDrawList *drawList = ImGui::GetWindowDrawList();
                ImGui::PushClipRect(pos, ImVec2(pos.x + available, pos.y + ImGui::GetTextLineHeight()), true);
                ImVec4 color = latestColor;
                color.w = hovered ? 1.0f : 0.8f;
                drawList->AddText(pos, ImGui::GetColorU32(color), latest.c_str());
                ImGui::PopClipRect();
            }

            ImGui::SameLine(std::max(rightStart, ImGui::GetCursorPosX() + 10.0f));

            // ---- フレーム時間の小さなグラフ（直近120フレーム。16.7ms の線より上に出たら重いフレーム）----
            {
                constexpr int kSamples = 120;
                static float frameTimes[kSamples] = {};
                static int head = 0;
                frameTimes[head] = io.DeltaTime * 1000.0f;
                head = (head + 1) % kSamples;

                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const float height = ImGui::GetTextLineHeight();
                ImGui::Dummy(ImVec2(kSparkWidth, height));
                const bool hovered = ImGui::IsItemHovered();
                ImDrawList *drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(origin, ImVec2(origin.x + kSparkWidth, origin.y + height), IM_COL32(255, 255, 255, 12), 2.0f);

                // 縦軸は 0〜33.3ms（30fps）固定。それ以上は上端で切る
                constexpr float kMaxMs = 33.3f;
                auto toY = [&](float ms) { return origin.y + height - std::min(ms / kMaxMs, 1.0f) * height; };
                const float targetY = toY(1000.0f / 60.0f);
                drawList->AddLine(ImVec2(origin.x, targetY), ImVec2(origin.x + kSparkWidth, targetY), IM_COL32(255, 255, 255, 40));

                float worst = 0.0f;
                float sum = 0.0f;
                ImVec2 previous{};
                for (int i = 0; i < kSamples; ++i)
                {
                    const float ms = frameTimes[(head + i) % kSamples];
                    worst = std::max(worst, ms);
                    sum += ms;
                    const ImVec2 point(origin.x + kSparkWidth * static_cast<float>(i) / static_cast<float>(kSamples - 1), toY(ms));
                    if (i > 0)
                    {
                        const ImU32 color = (ms > 1000.0f / 30.0f)   ? IM_COL32(235, 107, 102, 255)
                                            : (ms > 1000.0f / 55.0f) ? IM_COL32(242, 191, 89, 255)
                                                                     : IM_COL32(115, 204, 133, 255);
                        drawList->AddLine(previous, point, color, 1.0f);
                    }
                    previous = point;
                }
                if (hovered)
                {
                    ImGui::SetTooltip("直近 %d フレームの処理時間\n平均 %.1f ms / 最大 %.1f ms\n横線は 16.7 ms（60fps）", kSamples,
                                      sum / static_cast<float>(kSamples), worst);
                }
                ImGui::SameLine(0.0f, 8.0f);
            }

            const ImVec4 fpsColor = (fps >= 55.0f) ? ImVec4(0.45f, 0.80f, 0.52f, 1.0f)
                                    : (fps >= 30.0f) ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f)
                                                     : ImVec4(0.92f, 0.42f, 0.40f, 1.0f);
            ImGui::TextColored(fpsColor, "%s", frameText.c_str());
            ImGui::SameLine(0.0f, 16.0f);
            if (ImGui::SmallButton(paletteLabel))
            {
                commandPalette_->Open();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("コマンドパレット: 窓・操作・オブジェクト・シーンを名前で探して実行します");
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void ImGuiManager::ShowNotificationWindow()
{
    if (!showNotificationView_)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 360.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_BELL " 通知の履歴", &showNotificationView_))
    {
        ImGui::End();
        return;
    }

    static char filter[128] = {};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::InputTextWithHint("##filter", ICON_FA_FILTER " 絞り込み", filter, sizeof(filter));
    ImGui::SameLine();
    if (DangerButton(ICON_FA_TRASH_ALT " 消去"))
    {
        ImGuiNotification::ClearHistory();
    }

    const auto &history = ImGuiNotification::GetHistory();
    ImGui::TextDisabled("%d 件（新しい順・右クリックでコピー）", static_cast<int>(history.size()));
    if (ImGui::BeginChild("##history", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i)
        {
            const ImGuiNotification::Notification &n = history[i];
            if (filter[0] != '\0' && n.message.find(filter) == std::string::npos)
            {
                continue;
            }
            ImGui::PushID(i);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(ImVec2(pos.x, pos.y + 2.0f), ImVec2(pos.x + 3.0f, pos.y + ImGui::GetTextLineHeight() - 2.0f),
                                    ImGui::GetColorU32(ImVec4(n.color.x, n.color.y, n.color.z, 1.0f)), 1.5f);
            ImGui::Indent(10.0f);
            ImGui::TextDisabled("%s", n.time.c_str());
            ImGui::SameLine();
            ImGui::TextWrapped("%s", n.message.c_str());
            ImGui::Unindent(10.0f);
            if (ImGui::BeginPopupContextItem("##copy"))
            {
                if (ImGui::MenuItem(ICON_FA_COPY " コピー"))
                {
                    ImGui::SetClipboardText(n.message.c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void ImGuiManager::ShowAppearanceWindow()
{
    if (!showAppearanceView_ || !appearance_)
    {
        return;
    }
    if (appearance_->DrawWindow(&showAppearanceView_))
    {
        appearanceDirty_ = true;
    }
}

void ImGuiManager::ApplyAppearanceIfDirty()
{
    if (!appearanceDirty_ || !appearance_)
    {
        return;
    }
    appearanceDirty_ = false;
    // 倍率を掛け重ねないよう、いったん既定へ戻してから組み直す
    ImGui::GetStyle() = ImGuiStyle();
    SetupTheme();
    appearance_->ApplyTo(ImGui::GetStyle());
}

void ImGuiManager::ShowCommandPalette()
{
    if (!commandPalette_)
    {
        return;
    }
    // どの窓にフォーカスがあっても効くように、ルートをグローバルにする
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_K, ImGuiInputFlags_RouteGlobal))
    {
        commandPalette_->Toggle();
    }
    if (!commandPalette_->WantsCommands())
    {
        return;
    }
    std::vector<EditorCommand> commands;
    BuildPaletteCommands(commands);
    commandPalette_->Draw(commands);
}

void ImGuiManager::BuildPaletteCommands(std::vector<EditorCommand> &out)
{
    using Kind = EditorCommand::Kind;
    auto add = [&](Kind kind, const char *icon, std::string label, std::string hint, std::string shortcut, std::function<void()> action,
                   bool checked = false) {
        EditorCommand command;
        command.kind = kind;
        command.icon = icon;
        command.label = std::move(label);
        command.hint = std::move(hint);
        command.shortcut = std::move(shortcut);
        command.action = std::move(action);
        command.checked = checked;
        out.push_back(std::move(command));
    };

    // ---- 操作 ----
    UndoRedoManager *undo = UndoRedoManager::GetInstance();
    if (undo->CanUndo())
    {
        add(Kind::Action, ICON_FA_UNDO, "元に戻す", undo->GetUndoLabel() + " undo", "Ctrl+Z", [undo] {
            const std::string label = undo->GetUndoLabel();
            if (undo->Undo())
            {
                ImGuiNotification::Post("元に戻す: " + label, {0.42f, 0.66f, 0.68f, 1.0f});
            }
        });
    }
    if (undo->CanRedo())
    {
        add(Kind::Action, ICON_FA_REDO, "やり直し", undo->GetRedoLabel() + " redo", "Ctrl+Y", [undo] {
            const std::string label = undo->GetRedoLabel();
            if (undo->Redo())
            {
                ImGuiNotification::Post("やり直し: " + label, {0.42f, 0.66f, 0.68f, 1.0f});
            }
        });
    }

    PlayModeManager *playMode = PlayModeManager::GetInstance();
    add(Kind::Action, playMode->IsPlaying() ? ICON_FA_PAUSE : ICON_FA_PLAY, playMode->IsPlaying() ? "一時停止" : "再生",
        "play pause プレイ", "Ctrl+P", [playMode] {
            if (playMode->IsPlaying())
            {
                playMode->Pause();
            }
            else
            {
                playMode->Play();
            }
        });
    add(Kind::Action, ICON_FA_STOP, "停止（再生前の状態へ戻す）", "stop", "Ctrl+Shift+P", [playMode] { playMode->Stop(); });
    add(Kind::Action, ICON_FA_STEP_FORWARD, "コマ送り", "step 1フレーム", "", [playMode] { playMode->StepOnce(); });

    // シーンのクリック対象
    if (pImGuizmoManager_)
    {
        struct PickEntry
        {
            GizmoCategory category;
            const char *icon;
            const char *label;
            const char *shortcut;
        };
        static const PickEntry kPickEntries[] = {
            {GizmoCategory::Object, ICON_FA_CUBE, "クリック対象: オブジェクトだけ", "Alt+1"},
            {GizmoCategory::Sprite, ICON_FA_IMAGE, "クリック対象: スプライトだけ", "Alt+2"},
            {GizmoCategory::Particle, ICON_FA_STAR, "クリック対象: パーティクルだけ", "Alt+3"},
            {GizmoCategory::Light, ICON_FA_LIGHTBULB, "クリック対象: ライトだけ", "Alt+4"},
        };
        for (const PickEntry &entry : kPickEntries)
        {
            const bool solo = pImGuizmoManager_->GetCategoryMask() == (1u << static_cast<uint32_t>(entry.category));
            add(Kind::Setting, entry.icon, entry.label, "pick filter 選択 フィルタ ソロ", entry.shortcut,
                [this, entry] { pImGuizmoManager_->SoloCategory(entry.category); }, solo);
        }
        add(Kind::Setting, ICON_FA_MOUSE_POINTER, "クリック対象: すべて", "pick filter 選択 フィルタ all", "Alt+0",
            [this] { pImGuizmoManager_->EnableAllCategories(); },
            pImGuizmoManager_->GetCategoryMask() == (1u << kGizmoCategoryCount) - 1u);
    }

    add(Kind::Action, ICON_FA_SAVE, "シーンを保存", "save 上書き", "Ctrl+S", [] { SceneSerializer::GetInstance()->SaveCurrentScene(); });
    add(Kind::Action, ICON_FA_DOWNLOAD, "名前を付けてシーンを保存", "save as 別名", "Ctrl+Shift+S", [] { SceneSerializer::GetInstance()->OpenSaveDialog(); });
    add(Kind::Action, ICON_FA_UPLOAD, "シーンを読み込む", "load open", "Ctrl+Shift+L", [] { SceneSerializer::GetInstance()->OpenLoadDialog(); });
    add(Kind::Action, ICON_FA_PLUS, "新規オブジェクト", "create new model", "Ctrl+Shift+N", [this] { pBaseObjectManager_->OpenObjectCreationModal(); });
    add(Kind::Action, ICON_FA_SQUARE, "スプライトを作る", "sprite 2D", "", [this] { pSpriteManager_->ShowSpriteCreationModal(); });

    struct PrimitiveEntry
    {
        const char *label;
        PrimitiveType type;
        const char *baseName;
    };
    static const PrimitiveEntry kPrimitives[] = {
        {"キューブを置く", PrimitiveType::Cube, "cube"},     {"球体を置く", PrimitiveType::Sphere, "sphere"},
        {"平面を置く", PrimitiveType::Plane, "plane"},       {"シリンダーを置く", PrimitiveType::Cylinder, "cylinder"},
        {"リングを置く", PrimitiveType::Ring, "ring"},       {"ピラミッドを置く", PrimitiveType::Pyramid, "pyramid"},
        {"円錐を置く", PrimitiveType::Cone, "cone"},         {"岩を置く", PrimitiveType::Rock, "rock"},
    };
    for (const PrimitiveEntry &entry : kPrimitives)
    {
        add(Kind::Action, ICON_FA_CUBE, entry.label, std::string("primitive プリミティブ ") + entry.baseName, "", [this, entry] {
            if (BaseObject *created = pBaseObjectManager_->CreatePrimitiveObject(entry.type, entry.baseName))
            {
                pImGuizmoManager_->SelectOnly(created->GetName());
            }
        });
    }

    if (pImGuizmoManager_ && !pImGuizmoManager_->GetSelectedNames().empty())
    {
        add(Kind::Action, ICON_FA_CROSSHAIRS, "選択へカメラを寄せる", "focus フォーカス", "F", [this] { pImGuizmoManager_->FocusOnSelection(); });
        add(Kind::Action, ICON_FA_CLONE, "選択を複製", "duplicate コピー", "Ctrl+D", [this] { pImGuizmoManager_->DuplicateSelectedObjects(); });
        add(Kind::Action, ICON_FA_COPY, "選択をコピー", "copy", "Ctrl+C", [this] { pImGuizmoManager_->CopySelectedObjects(); });
        add(Kind::Action, ICON_FA_TRASH_ALT, "選択を削除", "delete remove", "Delete", [this] { pImGuizmoManager_->DeleteSelectedObjects(); });
        if (BaseObject *selected = pImGuizmoManager_->GetSelectedTarget())
        {
            const std::string name = selected->GetName();
            add(Kind::Action, ICON_FA_BOX, "選択をプレハブとして保存", "prefab save テンプレート", "",
                [this, name] { pImGuizmoManager_->OpenPrefabSaveDialog(name); });
        }
    }
    // 保存済みのプレハブをカメラの前に置く
    for (const std::string &prefab : pBaseObjectManager_->ListPrefabNames())
    {
        add(Kind::Action, ICON_FA_BOX, "プレハブを置く: " + prefab, "prefab place 配置", "",
            [this, prefab] { pImGuizmoManager_->PlacePrefab(prefab, pImGuizmoManager_->GetSpawnPosition()); });
    }
    add(Kind::Action, ICON_FA_PASTE, "貼り付け", "paste", "Ctrl+V", [this] { pImGuizmoManager_->PasteObjects(); });

    if (pCurrentScene_)
    {
        add(Kind::Action, ICON_FA_VIDEO, "デバッグカメラの切り替え", "debug camera 自由視点", "F3", [this] {
            if (pCurrentScene_)
            {
                const bool active = pCurrentScene_->ToggleDebugCamera();
                ImGuiNotification::Post(active ? "デバッグカメラ: ON" : "デバッグカメラ: OFF", {0.45f, 0.68f, 0.52f, 1.0f});
            }
        });
    }
    add(Kind::Action, ICON_FA_CAMERA, "スクリーンショットを撮る", "screenshot capture 撮影", "F9",
        [] { CaptureManager::GetInstance()->RequestScreenshot(); });
    add(Kind::Action, ICON_FA_CIRCLE, CaptureManager::GetInstance()->IsRecording() ? "連番録画を止める" : "連番録画を始める",
        "record 録画 動画", "F10", [] {
            CaptureManager *capture = CaptureManager::GetInstance();
            if (capture->IsRecording())
            {
                capture->StopSequence();
            }
            else
            {
                capture->StartSequence();
            }
        });
    add(Kind::Action, isShowMainUI_ ? ICON_FA_GAMEPAD : ICON_FA_WRENCH, isShowMainUI_ ? "ゲームモードに切り替え" : "エディターモードに切り替え",
        "mode 表示", "F5", [this] {
            isShowMainUI_ = !isShowMainUI_;
            if (isShowMainUI_)
            {
                SwitchToEditorMode();
            }
            else
            {
                SwitchToGameMode();
            }
        });
    add(Kind::Action, ICON_FA_EXPAND, "フルスクリーン切り替え", "fullscreen", "F11", [this] { pWinApp_->ToggleFullScreen(); });
    add(Kind::Action, ICON_FA_BORDER_ALL, "グリッド表示", "grid", "", [this] { showGrid_ = !showGrid_; }, showGrid_);
    add(Kind::Action, ICON_FA_TOOLS, "シーンのツールバー表示", "overlay toolbar ギズモ", "",
        [this] { showSceneOverlay_ = !showSceneOverlay_; }, showSceneOverlay_);
    add(Kind::Action, ICON_FA_CUBE, "シーンの軸の向き表示", "axis view cube 視点", "", [this] { showViewAxis_ = !showViewAxis_; },
        showViewAxis_);
    add(Kind::Action, ICON_FA_KEYBOARD, "ショートカット一覧", "help keys ヘルプ", "F1", [this] { showShortcutWindow_ = true; });
    add(Kind::Action, ICON_FA_SAVE, "UI設定を保存", "save layout 窓", "", [this] {
        SaveFlag();
        SaveCurrentLayout();
    });

    // ---- ウィンドウ ----
    for (const EditorWindowEntry &entry : windowRegistry_)
    {
        bool *flag = entry.flag;
        add(Kind::Window, entry.icon, entry.label, std::string(entry.tip) + " " + entry.category, "", [flag] { *flag = !*flag; }, *flag);
    }

    // ---- アセット（何か打つか、先頭に $ を付けたときだけ一覧に出る）----
    if (assetBrowser_)
    {
        for (const EditorAssetBrowser::Entry &entry : assetBrowser_->GetEntries())
        {
            const char *verb = nullptr;
            const char *icon = ICON_FA_FILE;
            switch (entry.kind)
            {
            case EditorAssetBrowser::Kind::Model:
                verb = "モデルを置く: ";
                icon = ICON_FA_CUBE;
                break;
            case EditorAssetBrowser::Kind::Sound:
                verb = "試聴: ";
                icon = ICON_FA_MUSIC;
                break;
            case EditorAssetBrowser::Kind::Image:
                verb = "画像のパスをコピー: ";
                icon = ICON_FA_IMAGE;
                break;
            default:
                break; // プレハブは「プレハブを置く」、JSON は出さない
            }
            if (!verb)
            {
                continue;
            }
            EditorAssetBrowser *browser = assetBrowser_.get();
            add(Kind::Asset, icon, verb + entry.relPath, "asset アセット", "", [browser, entry] { browser->ActivateEntry(entry); });
        }
    }

    // ---- ワークスペース ----
    for (int i = 0; i < static_cast<int>(workspaces_.size()); ++i)
    {
        add(Kind::Workspace, workspaces_[i].icon, std::string("ワークスペース: ") + workspaces_[i].name, workspaces_[i].description, "",
            [this, i] { ApplyWorkspace(i); }, currentWorkspace_ == i);
    }

    // ---- 外観 ----
    if (appearance_)
    {
        for (int i = 0; i < EditorAppearance::kThemeCount; ++i)
        {
            add(Kind::Setting, ICON_FA_PALETTE, std::string("テーマ: ") + EditorAppearance::GetThemeName(i), "theme 配色 色", "",
                [this, i] {
                    appearance_->SetTheme(i);
                    appearanceDirty_ = true;
                },
                appearance_->GetSettings().theme == i);
        }
        for (int i = 0; i < EditorAppearance::kAccentCount - 1; ++i)
        {
            add(Kind::Setting, ICON_FA_TINT, std::string("アクセント色: ") + EditorAppearance::GetAccentName(i), "accent 色", "",
                [this, i] {
                    appearance_->SetAccent(i);
                    appearanceDirty_ = true;
                },
                appearance_->GetSettings().accent == i);
        }
    }

    // ---- シーン ----
    const std::vector<std::string> sceneNames = SceneRegistry::GetInstance()->GetSceneNames();
    for (size_t i = 0; i < sceneNames.size(); ++i)
    {
        const std::string sceneName = sceneNames[i];
        add(Kind::Scene, ICON_FA_GAMEPAD, sceneName + " シーンへ", "scene シーン切り替え", (i < 9) ? "Ctrl+" + std::to_string(i + 1) : "",
            [sceneName] {
                SceneManager::GetInstance()->NextSceneReservation(sceneName);
                ImGuiNotification::Post(sceneName + " シーンへ移行します", {0.4f, 0.8f, 1.0f, 1.0f});
            },
            SceneManager::GetInstance()->GetCurrentSceneName() == sceneName);
    }

    // ---- オブジェクト（選んでカメラを寄せ、インスペクタを開く）----
    for (const std::string &name : pBaseObjectManager_->GetSortedObjectNames())
    {
        add(Kind::Object, ICON_FA_CUBE, name, "object 選択", "",
            [this, name] {
                pImGuizmoManager_->SelectOnly(name);
                pImGuizmoManager_->FocusOnSelection();
                showObjectView_ = true;
            },
            pImGuizmoManager_ && pImGuizmoManager_->IsSelected(name));
    }
}

} // namespace Hagine
#endif // USE_IMGUI
