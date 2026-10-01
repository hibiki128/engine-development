#include "ImGuiManager.h"
#ifdef USE_IMGUI
#include "2d/text/TextRenderer.h"
#include "2d/ui/UIAnimator.h"
#include "AssetDragDrop.h"
#include <edit/play/PlayModeManager.h>
#include <browser/ShowFolder.h>
#include "DebugUIHelper.h"
#include "ImGuiNotification.h"
#include "ImGuizmo.h"
#include "ImGuizmoManager.h"
#include "ShaderEditorWindow.h"
#include "collider/CollisionManager.h"
#include "edit/motion/MotionEditor.h"
#include "edit/behavior/BehaviorTreeEditor.h"
#include "edit/animation/AnimationStateMachineEditor.h"
#include <render/SceneViewRenderer.h>
#include "graphics/texture/TextureManager.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "camera/CameraManager.h"
#include "object/Object3dInstancing.h"
#include "render/RenderCulling.h"
#include "object/base/BaseObject.h"
#include "offscreen/OffScreen.h"
#include "scene/SceneManager.h"
#include "scene/SceneSerializer.h"
#include <algorithm>
#include <asset/AssetPath.h>
#include <data/DataHandler.h>
#include <debug/log/Logger.h>
#include <debug/param/GameParamHub.h>
#include <debug/profiler/CpuProfiler.h>
#include <debug/profiler/GpuProfiler.h>
#include <edit/undo/UndoRedoManager.h>
#include <filesystem>
#include <format>
#include <frame/Frame.h>
#include <icon/IconsFontAwesome5.h>
#include <imgui_internal.h>
#include <imgui_impl_dx12.h>
#include <implot.h>
#include <implot3d.h>
#include <line/LineRenderer.h>
#include <map>
#include <debug/capture/CaptureManager.h>
#include <debug/console/DebugConsole.h>
#include <edit/timeline/TimelineEditor.h>
#include <music/MusicEditor.h>
#include <particle/gpu/ParticleCSFieldManager.h>
#include <particle/gpu/ParticleCSSpawner.h>
#include <render/DrawSystem.h>
#include <shadow/ShadowMap.h>
#endif // USE_IMGUI

namespace Hagine {
#ifdef USE_IMGUI

namespace {
// ImGui DX12 バックエンド（マルチビューポート対応の新API）がSRVデスクリプタを
// 動的に確保/解放するためのコールバック。SrvManager の共有ヒープから割り当てる。
//
// ★重要: エンジン全体は「予約した r に対し実書き込みは r+1」という +1 規約で統一されている
//   （Sprite/Skin/Particle 各種/RendererBuffer/TextureManager(kSRVIndexTop=1) 等）。
//   ImGui のフォントSRVも実行時に動的確保されるため、ここで +1 を付けないと +1 利用箇所と
//   同じデスクリプタに書き込んでしまい、フォントSRVが上書きされて文字・塗りが黒化する。
static constexpr uint32_t kImGuiSrvOffset = 1;
void ImGuiSrvAlloc(ImGui_ImplDX12_InitInfo * /*info*/,
                   D3D12_CPU_DESCRIPTOR_HANDLE *outCpu, D3D12_GPU_DESCRIPTOR_HANDLE *outGpu) {
    SrvManager *srv = SrvManager::GetInstance();

    // 実際に書き込む枠（reserved + 1）も自分で押さえておく。
    // 押さえずにいると、後から Allocate した誰かがそこを「自分の予約枠」として受け取り、
    // +1 規約を守らない実装だとそこへ直接書き込んでフォントアトラスのSRVを潰す。
    // （実際に MetaBallGpuField がこれをやっていて、シーンを作り直すと
    //   GUI が丸ごと見えなくなる不具合になっていた）
    //
    // 空きリストから配られると番号が連続しない。連続した2枠が取れるまで前へずらす
    // （ずらして捨てた番号はそのまま使われないだけで、次の解放とは無関係なので安全）
    uint32_t reserved = srv->Allocate();
    uint32_t claimed = srv->Allocate();
    for (int retry = 0; claimed != reserved + 1 && retry < 64; ++retry)
    {
        reserved = claimed;
        claimed = srv->Allocate();
    }
    if (claimed != reserved + 1)
    {
        Logger::Error("ImGui: SRVの書き込み枠を連続で確保できませんでした（予約 " +
                      std::to_string(reserved) + " / 実際 " + std::to_string(claimed) + "）");
    }

    const uint32_t index = reserved + kImGuiSrvOffset;
    *outCpu = srv->GetCPUDescriptorHandle(index);
    *outGpu = srv->GetGPUDescriptorHandle(index);
}
void ImGuiSrvFree(ImGui_ImplDX12_InitInfo * /*info*/,
                  D3D12_CPU_DESCRIPTOR_HANDLE /*cpu*/, D3D12_GPU_DESCRIPTOR_HANDLE /*gpu*/) {
    // 解放はあえて no-op（インデックスをプールへ戻さない）。
    // ImGui の1枠につき予約を2つ（reserved と claimed）押さえているので、片方だけ戻すと
    // 対の関係が崩れる。フォントアトラスの再構築でしか呼ばれず（=ごく少数）、
    // リークは数枠程度で無害なため戻さない方が安全。
}
} // namespace

void ImGuiManager::Initialize(WinApp *winApp, ImGuizmoManager *imguizmoManager) {

    pWinApp_ = winApp;
    pDxCommon_ = DirectXCommon::GetInstance();
    pBaseObjectManager_ = BaseObjectManager::GetInstance();
    pSpriteManager_ = SpriteManager::GetInstance();
    pAudio_ = Audio::GetInstance();
    // ウィンドウ一覧は表示フラグのアドレスを覚えるだけなので、読み込みより先に作ってよい
    BuildWindowRegistry();
    BuildWorkspaces();
    LoadFlag();
    // ImGuiのコンテキストを生成
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot3D::CreateContext(); // モーション軌跡の3Dプレビュー用

    editorIniFilePath_ = AssetPath::Config("imgui_editor.ini");
    gameIniFilePath_ = AssetPath::Config("imgui_game.ini");

    // Docking機能を有効化
    ImGuiIO &io = ImGui::GetIO();
    // 高度な機能を有効化
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // ドッキング機能
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // マルチビューポート（ImGuiウィンドウを独立OSウィンドウへ）
    io.ConfigWindowsResizeFromEdges = true;             // エッジからリサイズ
    io.ConfigWindowsMoveFromTitleBarOnly = true;        // タイトルバーからの移動
    multiViewport_ = (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;

    // パフォーマンス関連の設定
    io.ConfigMemoryCompactTimer = 300.0f; // メモリ圧縮の間隔を長く
    io.IniFilename = nullptr;             // 設定ファイルの保存場所
                                          // 設定ファイルの保存場所
    LoadLayoutForCurrentMode();

    io.Fonts->Clear(); // 既存のフォントをクリア

    // 本文とアイコンで同じ大きさを使う。
    // 以前は本文 14px・アイコン 16px と食い違っており、アイコン付きの
    // メニュー項目やボタンでアイコンだけ大きく／文字とベースラインがずれて見えていた。
    const float fontSize = 15.0f;

    // フォント読み込み。ファイルが見つからないと ImGui は assert で強制終了してしまうため、
    // ImFontFlags_NoLoadError を立てて戻り値で成否を判定し、失敗はログに残して起動を続行する。
    auto loadFont = [](const std::string &path, float sizePixels, ImFontConfig config, const ImWchar *ranges) -> ImFont * {
        config.Flags |= ImFontFlags_NoLoadError;
        ImFont *font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), sizePixels, &config, ranges);
        if (font == nullptr)
        {
            Logger::Error("フォントの読み込みに失敗しました: " + path);
        }
        return font;
    };

    // 日本語対応の基本フォント。読み込めなければ ImGui 内蔵フォントで代替する
    // (この後のアイコンフォントはマージ指定なので、土台になるフォントが必ず要る)。
    if (loadFont(AssetPath::Font("NotoSansJP-Medium.ttf"), fontSize, ImFontConfig(), io.Fonts->GetGlyphRangesJapanese()) == nullptr)
    {
        io.Fonts->AddFontDefault();
    }

    // アイコンフォント読み込み（FontAwesomeなど）
    // 本文へマージするので、大きさは本文と同じにする。
    // GlyphMinAdvanceX を本文サイズにそろえると、アイコン幅が一定になり
    // 「アイコン + 文字」のメニュー項目で文字の開始位置がそろう。
    static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = fontSize;
    // アイコンは本文より少し上に付きがちなので、1px 下げて文字とベースラインを合わせる
    icons_config.GlyphOffset = ImVec2(0.0f, 1.0f);
    loadFont(AssetPath::Font("fa-solid-900.ttf"), fontSize, icons_config, icon_ranges);

    // ImGui 1.92 の新DX12バックエンド（ImGui_ImplDX12_InitInfo）は
    // ImGuiBackendFlags_RendererHasTextures を立て、フォントアトラスを動的管理する。
    // その場合 ImFontAtlas::Build()（= GetTexDataAsRGBA32）を手動で呼ぶとアサートになるため呼ばない。
    // フォントテクスチャは必要時にバックエンドが自動でラスタライズ／アップロードする。

    // カスタムテーマを設定し、その上に保存済みの外観（配色・大きさ）を重ねる
    SetupTheme();
    appearance_ = std::make_unique<EditorAppearance>();
    appearance_->Load();
    appearance_->ApplyTo(ImGui::GetStyle());
    commandPalette_ = std::make_unique<EditorCommandPalette>();
    assetBrowser_ = std::make_unique<EditorAssetBrowser>();
    assetBrowser_->Initialize();

    ImGui_ImplWin32_Init(winApp->GetHwnd());

    // SRVを割り当てる管理クラスを取得
    pSrvManager_ = SrvManager::GetInstance();

    // マルチビューポート対応のため、新APIの InitInfo で初期化する。
    // SRVデスクリプタの確保/解放はコールバック経由で SrvManager に委譲し、
    // 副ウィンドウのフォント/テクスチャ用に追加SRVを動的確保できるようにする。
    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = pDxCommon_->GetDevice().Get();
    initInfo.CommandQueue = pDxCommon_->GetCommandQueue();
    initInfo.NumFramesInFlight = 2;
    // メインビューポートの ImGui はバックバッファの sRGB RTV に描画されるため、
    // ImGui の PSO も sRGB にしないと #613(RENDER_TARGET_FORMAT_MISMATCH) になる。
    // 副ビューポートのフリップモデル・スワップチェインは sRGB 不可だが、
    // imgui_impl_dx12.cpp 側を patch 済み（swapchain は非sRGB・RTV は明示sRGB）なので
    // ここは sRGB を渡してよい（メイン／副ともに sRGB-correct で一致する）。
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = pSrvManager_->GetDescriptorHeap();
    initInfo.SrvDescriptorAllocFn = ImGuiSrvAlloc;
    initInfo.SrvDescriptorFreeFn = ImGuiSrvFree;
    ImGui_ImplDX12_Init(&initInfo);

    pImGuizmoManager_ = imguizmoManager;
}

void ImGuiManager::SetupTheme() {
    // ------------------------------------------------------------------
    // near-black ダークテーマ
    //
    // 色は「sRGB でそのまま見える値」を書く。バックバッファが *_UNORM_SRGB の
    // ため、imgui_impl_dx12.cpp の頂点シェーダで sRGB → リニアへ戻してから
    // GPU の再変換に渡している（詳細はそちらのコメント）。
    //
    // 明度は段階で組む。ここを崩すと「どれが窓でどれが入力欄か」が読めなくなる:
    //   Base0: 台座（タイトルバー・ポップアップ・ドック余白）  一番暗い
    //   Base1: 窓の地（WindowBg）
    //   Base2: タブ・テーブル見出し
    //   Base3: 入力欄・ボタン
    //   Base4/5: ホバー・押下                                  一番明るい
    // ------------------------------------------------------------------
    ImGuiStyle &style = ImGui::GetStyle();

    // 無彩色の段階（わずかに青寄り。完全な無彩色より画面が締まる）
    const ImVec4 kBase0 = ImVec4(0.027f, 0.027f, 0.035f, 1.00f); // #070709
    const ImVec4 kBase1 = ImVec4(0.047f, 0.047f, 0.055f, 1.00f); // #0C0C0E  窓の地
    const ImVec4 kBase2 = ImVec4(0.078f, 0.082f, 0.094f, 1.00f); // #141518
    const ImVec4 kBase3 = ImVec4(0.114f, 0.118f, 0.137f, 1.00f); // #1D1E23  入力欄
    const ImVec4 kBase4 = ImVec4(0.153f, 0.161f, 0.184f, 1.00f); // #27292F  ホバー
    const ImVec4 kBase5 = ImVec4(0.204f, 0.212f, 0.239f, 1.00f); // #34363D  押下・つまみ

    // アクセントカラー（淡いスチールブルー）。色味はこの3段階に統一する
    const ImVec4 accentDim = ImVec4(0.369f, 0.471f, 0.580f, 1.00f);    // 通常
    const ImVec4 accent = ImVec4(0.435f, 0.541f, 0.659f, 1.00f);       // ホバー
    const ImVec4 accentBright = ImVec4(0.533f, 0.635f, 0.745f, 1.00f); // アクティブ

    // アクセントの濃さ違いを作るヘルパー（同じ色をあちこちで書き写さない）
    auto withAlpha = [](const ImVec4 &c, float a) { return ImVec4(c.x, c.y, c.z, a); };

    // カラースキーム
    ImVec4 *colors = style.Colors;

    // テキスト（純白を避けたやわらかいオフホワイト）
    colors[ImGuiCol_Text] = ImVec4(0.871f, 0.878f, 0.894f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.376f, 0.388f, 0.412f, 1.00f);

    // 背景・枠
    colors[ImGuiCol_WindowBg] = kBase1;
    // 子領域は窓の地よりわずかに暗くして「一段落ち込んだ枠」に見せる。
    // 一覧（ライト一覧・ヒエラルキー等）が窓の中で region として読めるようになる。
    // 差は 4/255 程度で、枠なしの単なるレイアウト用 BeginChild では目立たない。
    colors[ImGuiCol_ChildBg] = ImVec4(0.031f, 0.031f, 0.039f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(kBase0.x, kBase0.y, kBase0.z, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.196f, 0.204f, 0.231f, 0.65f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

    // 入力フィールド
    colors[ImGuiCol_FrameBg] = kBase3;
    colors[ImGuiCol_FrameBgHovered] = kBase4;
    colors[ImGuiCol_FrameBgActive] = kBase5;

    // タイトルバー・メニューバー
    colors[ImGuiCol_TitleBg] = kBase0;
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.086f, 0.098f, 0.118f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(kBase0.x, kBase0.y, kBase0.z, 0.80f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.063f, 0.063f, 0.074f, 1.00f);

    // スクロールバー（無彩色のグレー）
    colors[ImGuiCol_ScrollbarBg] = ImVec4(kBase0.x, kBase0.y, kBase0.z, 0.55f);
    colors[ImGuiCol_ScrollbarGrab] = kBase4;
    colors[ImGuiCol_ScrollbarGrabHovered] = kBase5;
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.263f, 0.275f, 0.306f, 1.00f);

    // チェック・スライダー（アクセント）
    colors[ImGuiCol_CheckMark] = accentBright;
    colors[ImGuiCol_CheckboxSelectedBg] = withAlpha(accentDim, 0.28f);
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentBright;

    // ボタン（無彩色ベース、ホバー時のみ淡くスチールへ寄せる）
    colors[ImGuiCol_Button] = kBase3;
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.196f, 0.251f, 0.318f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.161f, 0.208f, 0.267f, 1.00f);

    // ヘッダー（CollapsingHeader / Selectable など）
    colors[ImGuiCol_Header] = ImVec4(0.106f, 0.114f, 0.133f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.153f, 0.196f, 0.247f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.184f, 0.235f, 0.298f, 1.00f);

    // セパレータ
    colors[ImGuiCol_Separator] = ImVec4(0.184f, 0.192f, 0.216f, 0.60f);
    colors[ImGuiCol_SeparatorHovered] = withAlpha(accent, 0.60f);
    colors[ImGuiCol_SeparatorActive] = accentBright;

    // リサイズグリップ
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.184f, 0.192f, 0.216f, 0.40f);
    colors[ImGuiCol_ResizeGripHovered] = withAlpha(accent, 0.60f);
    colors[ImGuiCol_ResizeGripActive] = withAlpha(accentBright, 0.90f);

    // テキスト入力のキャレット
    colors[ImGuiCol_InputTextCursor] = accentBright;

    // タブ（選択タブは上の細線＝Overline で示す。ここを未設定にすると
    //       ImGui 既定の鮮やかな青が1本だけ残って浮くので必ず埋める）
    colors[ImGuiCol_Tab] = ImVec4(0.063f, 0.067f, 0.078f, 1.00f);
    colors[ImGuiCol_TabHovered] = withAlpha(accent, 0.45f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.129f, 0.161f, 0.204f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] = accentBright;
    colors[ImGuiCol_TabDimmed] = ImVec4(0.043f, 0.043f, 0.051f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.086f, 0.090f, 0.106f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.259f, 0.278f, 0.306f, 1.00f);

    // ドッキング
    colors[ImGuiCol_DockingPreview] = withAlpha(accent, 0.45f);
    colors[ImGuiCol_DockingEmptyBg] = kBase0;

    // プロット（アクセントに統一）
    colors[ImGuiCol_PlotLines] = accentBright;
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.643f, 0.737f, 0.847f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accentBright;

    // テーブル（未設定だと ImGui 既定の明るい青灰が出てテーマから浮く）
    colors[ImGuiCol_TableHeaderBg] = kBase2;
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.196f, 0.204f, 0.231f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.125f, 0.129f, 0.149f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.000f, 1.000f, 1.000f, 0.022f);

    // ツリー・リンク・未保存マーカー
    colors[ImGuiCol_TreeLines] = ImVec4(0.216f, 0.224f, 0.251f, 0.70f);
    colors[ImGuiCol_TextLink] = accentBright;
    colors[ImGuiCol_UnsavedMarker] = ImVec4(0.800f, 0.720f, 0.420f, 1.00f);

    // 選択範囲・ドラッグ＆ドロップ・ナビゲーション
    colors[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.32f);
    colors[ImGuiCol_DragDropTarget] = withAlpha(accentBright, 0.90f);
    colors[ImGuiCol_DragDropTargetBg] = withAlpha(accent, 0.15f);
    colors[ImGuiCol_NavCursor] = accentBright;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.020f, 0.020f, 0.027f, 0.65f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.008f, 0.008f, 0.016f, 0.70f);

    // ------------------------------------------------------------------
    // 余白・寸法
    // 「ボタンがずれて見える」の多くは、行の高さ（FramePadding.y から決まる）と
    // 行間（ItemSpacing.y）が噛み合っていないせい。ここを一箇所で決めておく。
    // ------------------------------------------------------------------
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 5);
    style.CellPadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 20;
    style.ScrollbarSize = 13;
    style.GrabMinSize = 11;
    style.ColumnsMinSpacing = 8;

    // 文字の置き方。既定では Selectable の文字が枠の上端寄りに付き、
    // 同じ行のボタンとベースラインがずれて見えるので、縦は中央に揃える。
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    // SeparatorText（「シーン・オブジェクト」等の区切り見出し）
    style.SeparatorTextBorderSize = 2.0f;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(18, 5);

    // 無効化した項目は薄くしすぎない（押せないのか読めないのか分からなくなる）
    style.DisabledAlpha = 0.45f;

    // タイトルバー左の折りたたみ矢印を出さない。
    // 出していると窓名の開始位置が窓ごとに変わってタブが不揃いに見える。
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // ドック分割のつまみ。細すぎると掴めず、太いと線に見えるのでこの辺り
    style.DockingSeparatorSize = 2.0f;

    // タブ。ドックが狭いと「描画シ…」のように名前がすぐ切れるので、
    // 非選択タブの ✕ はホバー時だけ出して、その幅を名前に回す。
    style.TabCloseButtonMinWidthUnselected = 0.0f;
    style.TabBarOverlineSize = 2.0f;

    // ツリー（ヒエラルキー）に親子の接続線を出す。
    // 入れ子が深いとインデントだけでは親子関係が追えない。
    style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
    style.TreeLinesSize = 1.0f;

    // 外観
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.TabBorderSize = 0;
    style.TabBarBorderSize = 1;

    // 丸み
    style.WindowRounding = 6;
    style.ChildRounding = 6;
    style.FrameRounding = 4;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 4;
    style.TabRounding = 4;

    // Viewportsの設定（マルチウィンドウモード）
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    SetupPlotTheme();
}

void ImGuiManager::SetupPlotTheme() {
    // ImPlot は ImGui とは別のスタイルを持っているので、こちらも合わせておく。
    // 揃えておかないと統計窓のグラフだけ地色・軸色が既定のままで浮く。
    ImPlotStyle &plot = ImPlot::GetStyle();
    ImVec4 *pc = plot.Colors;

    const ImVec4 accent = ImVec4(0.435f, 0.541f, 0.659f, 1.00f);

    pc[ImPlotCol_FrameBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f); // 窓の地に溶かす
    pc[ImPlotCol_PlotBg] = ImVec4(0.027f, 0.027f, 0.035f, 1.00f);
    pc[ImPlotCol_PlotBorder] = ImVec4(0.196f, 0.204f, 0.231f, 0.65f);
    pc[ImPlotCol_LegendBg] = ImVec4(0.027f, 0.027f, 0.035f, 0.94f);
    pc[ImPlotCol_LegendBorder] = ImVec4(0.196f, 0.204f, 0.231f, 0.65f);
    pc[ImPlotCol_LegendText] = ImVec4(0.871f, 0.878f, 0.894f, 1.00f);
    pc[ImPlotCol_TitleText] = ImVec4(0.871f, 0.878f, 0.894f, 1.00f);
    pc[ImPlotCol_InlayText] = ImVec4(0.871f, 0.878f, 0.894f, 1.00f);
    pc[ImPlotCol_AxisText] = ImVec4(0.596f, 0.612f, 0.643f, 1.00f);
    pc[ImPlotCol_AxisGrid] = ImVec4(0.259f, 0.267f, 0.298f, 0.35f);
    pc[ImPlotCol_AxisBgHovered] = ImVec4(0.153f, 0.161f, 0.184f, 1.00f);
    pc[ImPlotCol_AxisBgActive] = ImVec4(0.204f, 0.212f, 0.239f, 1.00f);
    pc[ImPlotCol_Selection] = accent;
    pc[ImPlotCol_Crosshairs] = ImVec4(0.596f, 0.612f, 0.643f, 0.60f);

    plot.PlotPadding = ImVec2(8, 6);
    plot.LabelPadding = ImVec2(4, 3);
    plot.LegendPadding = ImVec2(8, 6);
    plot.PlotBorderSize = 1.0f;
    plot.MinorAlpha = 0.20f;

    // ImPlot3D（モーション軌跡プレビュー）も同じ配色に合わせる
    ImPlot3DStyle &plot3d = ImPlot3D::GetStyle();
    ImVec4 *p3 = plot3d.Colors;
    p3[ImPlot3DCol_FrameBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    p3[ImPlot3DCol_PlotBg] = ImVec4(0.027f, 0.027f, 0.035f, 1.00f);
    p3[ImPlot3DCol_PlotBorder] = ImVec4(0.196f, 0.204f, 0.231f, 0.65f);
    p3[ImPlot3DCol_LegendBg] = ImVec4(0.027f, 0.027f, 0.035f, 0.94f);
    p3[ImPlot3DCol_LegendBorder] = ImVec4(0.196f, 0.204f, 0.231f, 0.65f);
    p3[ImPlot3DCol_LegendText] = ImVec4(0.871f, 0.878f, 0.894f, 1.00f);
    p3[ImPlot3DCol_TitleText] = ImVec4(0.871f, 0.878f, 0.894f, 1.00f);
    p3[ImPlot3DCol_InlayText] = ImVec4(0.871f, 0.878f, 0.894f, 1.00f);
    p3[ImPlot3DCol_AxisText] = ImVec4(0.596f, 0.612f, 0.643f, 1.00f);
    p3[ImPlot3DCol_AxisGrid] = ImVec4(0.259f, 0.267f, 0.298f, 0.35f);
}

void ImGuiManager::CreateDescriptorHeap() {
    HRESULT result;

    // デスクリプタヒープ設定
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    // デスクリプタヒープ生成
    result = pDxCommon_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));

    ImGui_ImplDX12_Init(
        pDxCommon_->GetDevice().Get(),
        static_cast<int>(pDxCommon_->GetBackBufferCount()),
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srvHeap_.Get(),
        srvHeap_->GetCPUDescriptorHandleForHeapStart(),
        srvHeap_->GetGPUDescriptorHandleForHeapStart());
}

void ImGuiManager::Finalize() {
    SaveCurrentLayout();
// 後始末
#ifdef USE_IMGUI
    standaloneBtEditor_.reset(); // ノードエディタは ImGui より先に片付ける
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot3D::DestroyContext();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
#endif // USE_IMGUI

    // デスクリプタヒープを解放
    srvHeap_.Reset();

    SaveFlag();
}

void ImGuiManager::Begin() {
    // 外観の変更はフレームの外で反映する（フレームの途中で文字の大きさを変えると描画が崩れる）
    ApplyAppearanceIfDirty();
    // ImGuiフレーム開始
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::End() {
    ImGuiNotification::Draw();
    // 描画前準備
    ImGui::Render();
}

void ImGuiManager::Draw() {
    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();

    //// デスクリプタヒープの配列をセットするコマンド
    // ID3D12DescriptorHeap *ppHeaps[] = {srvHeap_.Get()};
    // pCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    //  描画コマンドを発行
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCommandList);
}

void ImGuiManager::RenderMultiViewport() {
    if (!multiViewport_) {
        return;
    }
    // メインビューポートの描画・Present が済んだ後に、ドックから切り離した
    // ImGuiウィンドウ群を独立OSウィンドウとして更新・描画する。
    // DX12 バックエンドが各ウィンドウのスワップチェイン生成/描画/Present を内部で処理する。
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
}

void ImGuiManager::RebuildGridBatchIfNeeded() {
    if (gridBatch_ != kInvalidLineBatch && builtGridDivision_ == gridDivision_ && builtGridSize_ == gridSize_) {
        return;
    }
    if (gridDivision_ <= 0 || gridSize_ <= 0.0f) {
        return;
    }

    // Y=0のローカル座標で格子を作る（既定は分割1000＝2002本。毎フレーム積み直すと重い）
    const float interval = (gridSize_ * 2.0f) / static_cast<float>(gridDivision_);
    constexpr uint32_t kWhite = 0xFFFFFFFFu;

    std::vector<LineVertex> vertices;
    vertices.reserve(static_cast<size_t>(gridDivision_ + 1) * 4);
    for (int i = 0; i <= gridDivision_; ++i) {
        const float offset = -gridSize_ + static_cast<float>(i) * interval;
        // X方向の線（Zを移動）
        vertices.push_back({{-gridSize_, 0.0f, offset}, kWhite});
        vertices.push_back({{gridSize_, 0.0f, offset}, kWhite});
        // Z方向の線（Xを移動）
        vertices.push_back({{offset, 0.0f, -gridSize_}, kWhite});
        vertices.push_back({{offset, 0.0f, gridSize_}, kWhite});
    }

    LineRenderer *pLine = LineRenderer::GetInstance();
    if (gridBatch_ == kInvalidLineBatch) {
        gridBatch_ = pLine->CreateBatch(vertices.data(), static_cast<uint32_t>(vertices.size()));
    } else {
        pLine->UpdateBatch(gridBatch_, vertices.data(), static_cast<uint32_t>(vertices.size()));
    }
    builtGridDivision_ = gridDivision_;
    builtGridSize_ = gridSize_;
}

void ImGuiManager::UpdateIni() {
    if (showGrid_) {
        RebuildGridBatchIfNeeded();
        if (gridBatch_ != kInvalidLineBatch) {
            // バッチはY=0のローカル座標。Y位置はワールド行列、色はtintで差し替える
            LineRenderer::GetInstance()->SubmitBatch(gridBatch_, MakeTranslateMatrix({0.0f, gridY_, 0.0f}), gridColor_);
        }
    }
    if (!isShowMainUI_) {
        SwitchToGameMode();
    } else {
        SwitchToEditorMode();
    }
}

void ImGuiManager::ShowMainMenu() {
    // メインメニューバー作成
    if (ImGui::BeginMainMenuBar()) {
        // ファイルメニュー
        if (ImGui::BeginMenu(ICON_FA_FILE " ファイル")) {
            // シーン管理セクション
            SceneSerializer *sceneSerializer = SceneSerializer::GetInstance();
            const std::string saveLabel = std::format(ICON_FA_SAVE " シーンを保存（{}）", SceneSerializer::CurrentSceneName());
            if (ImGui::MenuItem(saveLabel.c_str(), "Ctrl+S")) {
                sceneSerializer->SaveCurrentScene();
            }
            if (ImGui::MenuItem(ICON_FA_DOWNLOAD " 名前を付けて保存...", "Ctrl+Shift+S")) {
                sceneSerializer->OpenSaveDialog();
            }
            if (ImGui::MenuItem(ICON_FA_UPLOAD " シーンを読み込む...", "Ctrl+Shift+L")) {
                sceneSerializer->OpenLoadDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_DOOR_OPEN " 終了", "Alt+F4")) {
                // アプリケーション終了処理
                pWinApp_->ClosedWindow(); // 終了メッセージ送信
            }
            ImGui::EndMenu();
        }

        // 編集メニュー
        if (ImGui::BeginMenu(ICON_FA_EDIT " 編集")) {
            UndoRedoManager *undoMgr = UndoRedoManager::GetInstance();

            // 元に戻す（次にUndoされる操作名を表示する）
            std::string undoLabel = ICON_FA_UNDO " 元に戻す";
            if (undoMgr->CanUndo()) {
                undoLabel += ": " + undoMgr->GetUndoLabel();
            }
            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, undoMgr->CanUndo())) {
                const std::string label = undoMgr->GetUndoLabel();
                if (undoMgr->Undo()) {
                    ImGuiNotification::Post("元に戻す: " + label, {0.42f, 0.66f, 0.68f, 1.0f});
                }
            }

            // やり直し（次にRedoされる操作名を表示する）
            std::string redoLabel = ICON_FA_REDO " やり直し";
            if (undoMgr->CanRedo()) {
                redoLabel += ": " + undoMgr->GetRedoLabel();
            }
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, undoMgr->CanRedo())) {
                const std::string label = undoMgr->GetRedoLabel();
                if (undoMgr->Redo()) {
                    ImGuiNotification::Post("やり直し: " + label, {0.42f, 0.66f, 0.68f, 1.0f});
                }
            }
            ImGui::Separator();
            // 選択オブジェクトのコピー/貼り付け/削除（ギズモの選択に対して実行する）
            if (ImGui::MenuItem(ICON_FA_COPY " 選択オブジェクトをコピー", "Ctrl+C")) {
                pImGuizmoManager_->CopySelectedObjects();
            }
            if (ImGui::MenuItem(ICON_FA_PASTE " 貼り付け", "Ctrl+V")) {
                pImGuizmoManager_->PasteObjects();
            }
            if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 選択オブジェクトを削除", "Delete")) {
                pImGuizmoManager_->DeleteSelectedObjects();
            }
            ImGui::EndMenu();
        }

        // 表示メニュー（中身は ImGuiManagerShell.cpp の DrawViewMenu）
        if (ImGui::BeginMenu(ICON_FA_EYE " 表示")) {
            DrawViewMenu();
            ImGui::EndMenu();
        }

        // オブジェクトメニュー
        if (ImGui::BeginMenu(ICON_FA_CUBE " オブジェクト")) {
            if (ImGui::MenuItem(ICON_FA_PLUS " 新規オブジェクト", "Ctrl+Shift+N")) {
                // 新規オブジェクト作成
                pBaseObjectManager_->OpenObjectCreationModal();
            }

            if (ImGui::MenuItem(ICON_FA_PLUS " オブジェクト読み込み", "Ctrl+Shift+M")) {
                // 新規オブジェクト作成
                pBaseObjectManager_->OpenObjectLoadModal();
            }

            // 3Dオブジェクト
            if (ImGui::BeginMenu(ICON_FA_CUBE " 3Dオブジェクト")) {
                // 生成の中身（名前の一意化・カメラ前方への配置）は BaseObjectManager 側に持たせ、
                // ここは「どの形状をどのラベルで出すか」の表だけにしておく
                struct PrimitiveMenuEntry {
                    const char *label;
                    PrimitiveType type;
                    const char *baseName;
                };
                static const PrimitiveMenuEntry kPrimitiveEntries[] = {
                    {ICON_FA_CUBE " キューブ", PrimitiveType::Cube, "cube"},
                    {ICON_FA_CIRCLE " 球体", PrimitiveType::Sphere, "sphere"},
                    {ICON_FA_CUBE " 平面", PrimitiveType::Plane, "plane"},
                    {ICON_FA_CIRCLE " シリンダー", PrimitiveType::Cylinder, "cylinder"},
                    {ICON_FA_RING " リング", PrimitiveType::Ring, "ring"},
                    {ICON_FA_CARET_UP " 三角形", PrimitiveType::Triangle, "triangle"},
                    {ICON_FA_MOUNTAIN " ピラミッド", PrimitiveType::Pyramid, "pyramid"},
                    {ICON_FA_CHART_AREA " 円柱", PrimitiveType::Cone, "cone"},
                    {ICON_FA_MOUNTAIN " 岩", PrimitiveType::Rock, "rock"},
                };
                for (const PrimitiveMenuEntry &entry : kPrimitiveEntries) {
                    if (ImGui::MenuItem(entry.label)) {
                        BaseObject *created = pBaseObjectManager_->CreatePrimitiveObject(entry.type, entry.baseName);
                        // 出した直後に触れるよう、生成したものを選択状態にする
                        if (created) {
                            pImGuizmoManager_->SelectOnly(created->GetName());
                        }
                    }
                }

                ImGui::Separator();

                // メタボールは形が固定でないのでプリミティブ表とは別扱い
                if (ImGui::MenuItem(ICON_FA_CIRCLE " メタボール")) {
                    BaseObject *created = pBaseObjectManager_->CreateMetaBallObject("metaball");
                    if (created) {
                        pImGuizmoManager_->SelectOnly(created->GetName());
                    }
                }
                ImGui::SetItemTooltip("球やカプセルを並べて融合させる。インスペクタの「メタボール」から要素を編集する");

                if (ImGui::MenuItem(ICON_FA_TRASH_ALT " オブジェクト全削除")) {
                    // RemoveAllObjects が自分の登録だけを解除する。
                    // ここで DeleteTarget()（＝全操作対象を消す）を呼ぶと、スプライト・ライト・
                    // パーティクルのギズモ登録まで巻き添えで消えてしまう。
                    pBaseObjectManager_->RemoveAllObjects();
                    ImGuiNotification::Post("全オブジェクトを削除しました", {0.9f, 0.7f, 0.2f, 1.0f});
                }

                ImGui::EndMenu();
            }

            // 2Dオブジェクト
            if (ImGui::BeginMenu(ICON_FA_SQUARE " 2Dオブジェクト")) {
                if (ImGui::MenuItem(ICON_FA_SQUARE " スプライト作成")) {
                    pSpriteManager_->ShowSpriteCreationModal();
                }
                if (ImGui::MenuItem(ICON_FA_FONT " 文字スプライト作成")) {
                    // 文字スプライトの作成UIはスプライトマネージャ窓内に表示される
                    showSpriteManagerView_ = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("スプライトマネージャ窓を開きます（そこで文字→画像を生成）");
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        // ヘルプメニュー
        if (ImGui::BeginMenu(ICON_FA_QUESTION_CIRCLE " ヘルプ")) {
            if (ImGui::MenuItem(ICON_FA_KEYBOARD " ショートカット一覧", "F1")) {
                showShortcutWindow_ = !showShortcutWindow_;
            }
            ImGui::EndMenu();
        }

        // シーンメニュー（SceneRegistry に自己登録された全シーンを列挙する）
        if (ImGui::BeginMenu(ICON_FA_GLOBE " シーン選択")) { // 地球アイコン（意味：全体メニュー）

            const std::vector<std::string> sceneNames = SceneRegistry::GetInstance()->GetSceneNames();
            for (size_t i = 0; i < sceneNames.size(); ++i) {
                const std::string &sceneName = sceneNames[i];
                const std::string label = std::string(ICON_FA_GAMEPAD " ") + sceneName;
                // Framework::RegisterShortcutKey と同じ名前順で Ctrl+数字 が割り当てられている
                const std::string shortcut = (i < 9) ? "Ctrl+" + std::to_string(i + 1) : "";
                if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str())) {
                    SceneManager::GetInstance()->NextSceneReservation(sceneName);
                    ImGuiNotification::Post(sceneName + " シーンへ移行します", {0.4f, 0.8f, 1.0f, 1.0f});
                }
            }

            ImGui::EndMenu();
        }

        // 再生 / 一時停止 / コマ送り / 停止。常に見える位置に置きたいのでメニューバーの右側へ。
        ImGui::SameLine(0.0f, 24.0f);
        PlayModeManager::GetInstance()->DrawToolbar();

        // コマンドパレット（窓・操作・オブジェクト・シーンを名前で探す）
        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::SmallButton(ICON_FA_SEARCH " 検索  Ctrl+K")) {
            commandPalette_->Open();
        }
        ImGui::SetItemTooltip("コマンドパレット: 窓・操作・オブジェクト・シーンを名前の一部で探して実行します");

        ImGui::EndMainMenuBar();
    }

    // ステータスバーはドックより先に作る（先に作った分だけドックの作業領域が狭まる）
    if (isShowMainUI_) {
        DrawStatusBar();
    }
}

void ImGuiManager::ShowSceneSettingWindow() {
    if (!showSceneView_)
        return; // 表示しない場合は早期リターン

    // 出現時にフォーカスを奪わない軽量フラグ
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("シーン設定", &showSceneView_, flags);

    pCurrentScene_->AddSceneSetting();

    ImGui::End();
}

void ImGuiManager::ShowObjectSettingWindow() {
    if (!showObjectView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("オブジェクト設定", &showObjectView_, flags);

    pCurrentScene_->AddObjectSetting();
    // pBaseObjectManager_->DrawImGui();

    ImGui::End();
}

void ImGuiManager::ShowParticleSettingWindow() {
    if (!showParticleView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("パーティクル設定", &showParticleView_, flags);

    pCurrentScene_->AddParticleSetting();

    ParticleCSFieldManager::GetInstance()->DrawImGui();

    // json を選んでシーンへ実行時配置する UI（エディタのエミッターはプレビュー窓にしか
    // 出ないので、ゲーム画面で確認したいときはこちらから出す）
    ParticleCSSpawner::GetInstance()->DrawImGui();

    ImGui::End();
}

void ImGuiManager::ShowParticlePreviewWindow() {
    // 表示メニューの「パーティクルプレビュー」でON/OFF。ウィンドウのXボタンとフラグを同期させる。
    if (!showParticlePreviewView_)
        return;
    ParticleCSEditor::GetInstance()->ShowPreviewWindow(&showParticlePreviewView_);
}

void ImGuiManager::ShowGameParamWindow() {
    GameParamHub::GetInstance()->DrawImGui(&showGameParamView_);
}

void ImGuiManager::ShowStatisticsWindow() {
    if (!showFPSView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("統計", &showFPSView_, flags);

    DisplayFPS();

    ParticleEditor::GetInstance()->SceneParticleCount();

    ParticleCSEditor::GetInstance()->ShowGPUParticleStatistics();

    // オブジェクトのインスタンシング描画（同じモデルを参照するものをまとめた結果）
    ImGui::Separator();
    if (ImGui::CollapsingHeader("インスタンシング描画")) {
        Object3dInstancing *instancing = Object3dInstancing::GetInstance();
        bool enabled = instancing->IsEnabled();
        if (ImGui::Checkbox("有効##instancing", &enabled))
            instancing->SetEnabled(enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("同じモデルを参照するオブジェクトを1回の描画にまとめます。\n"
                              "OFF にすると従来どおり1体ずつ描画します（見た目は変わりません）");
        ImGui::Text("バッチ数: %u  /  インスタンス数: %u", instancing->GetLastBatchCount(), instancing->GetLastInstanceCount());
        ImGui::Text("減らせた描画コール: %u", instancing->GetLastMergedDrawCount());
    }

    // 画面に入らないオブジェクトを描画から省いた結果
    ImGui::Separator();
    if (ImGui::CollapsingHeader("錐台カリング")) {
        RenderCulling::DrawImGui();
    }

    ImGui::Separator();
    CpuProfiler::GetInstance()->DrawImGui();
    CpuProfiler::GetInstance()->DrawFrameCaptureImGui();

    ImGui::Separator();
    GpuProfiler::GetInstance()->DrawImGui();
    GpuProfiler::GetInstance()->DrawFrameStatsImGui();

    ImGui::Separator();
    if (ImGui::CollapsingHeader("ログ履歴", ImGuiTreeNodeFlags_DefaultOpen)) {
        static ImGuiTextFilter logFilter;
        static bool autoScroll = true;

        // ツールバー: クリア / 自動スクロール / 絞り込み
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.25f, 0.25f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.32f, 0.32f, 0.95f));
        if (ImGui::SmallButton("クリア##log"))
            ImGuiNotification::ClearHistory();
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::Checkbox("自動スクロール##log", &autoScroll);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        logFilter.Draw("##logfilter");

        const auto &history = ImGuiNotification::GetHistory();
        int shown = 0;
        ImGui::BeginChild("LogScrollRegion", ImVec2(0, 200), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto &n : history) {
            if (!logFilter.PassFilter(n.message.c_str()))
                continue;
            ++shown;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(n.color.x, n.color.y, n.color.z, n.color.w));
            ImGui::TextUnformatted(n.message.c_str());
            ImGui::PopStyleColor();
        }
        // 新しいログがあれば自動スクロール（最下部に居るときだけ）
        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::Text("%d 件表示 / 全 %d 件", shown, static_cast<int>(history.size()));
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

void ImGuiManager::ShowOffScreenSettingWindow(OffScreen *pOffScreen) {
    if (!showOfScreenView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("オフスクリーン設定", &showOfScreenView_, flags);

    pOffScreen->Setting();

    ImGui::End();
}

void ImGuiManager::ShowLightSettingWindow() {
    if (!showLightView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("ライト設定", &showLightView_, flags);

    LightGroup::GetInstance()->DrawImGui();

    ImGui::End();
}

void ImGuiManager::ShowGizmoWindow() {
    if (!showGizmoView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("トランスフォームマネージャ", &showGizmoView_, flags);

    pImGuizmoManager_->DrawImGui();

    ImGui::End();
}

void ImGuiManager::ShowHierarchyWindow() {
    if (!showHierarchyView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("オブジェクトマネージャ", &showHierarchyView_, flags);

    pBaseObjectManager_->DrawHierarchyEditor();

    ImGui::End();
}

void ImGuiManager::ShowMotionEditorWindow() {
    if (!showMotionEditorView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("モーションエディター", &showMotionEditorView_, flags);

    MotionEditor::GetInstance()->DrawImGui();

    ImGui::End();
}

void ImGuiManager::DrawGridMenu() {
    if (ImGui::BeginMenu(ICON_FA_BORDER_ALL " グリッド設定")) {
        // グリッド表示のON/OFFチェックボックス
        ImGui::MenuItem(ICON_FA_BORDER_ALL " グリッド表示", nullptr, &showGrid_);

        if (showGrid_) {
            ImGui::Separator();

            // Y座標設定
            ImGui::PushItemWidth(120.0f);
            if (ImGui::DragFloat(ICON_FA_ARROWS_ALT_V " Y座標", &gridY_, 0.1f, -100.0f, 100.0f, "%.1f")) {
            }

            // 分割数設定
            if (ImGui::DragInt(ICON_FA_TH " 分割数", &gridDivision_, 1, 1, 100)) {
            }

            // サイズ設定
            if (ImGui::DragFloat(ICON_FA_EXPAND_ARROWS_ALT " サイズ", &gridSize_, 0.1f, 0.1f, 500.0f, "%.1f")) {
            }
            ImGui::PopItemWidth();

            // 色設定
            ImGui::ColorEdit4(ICON_FA_PALETTE " グリッド色", &gridColor_.x, ImGuiColorEditFlags_NoInputs);

            // プリセット（サブメニュー）
            if (ImGui::BeginMenu(ICON_FA_SWATCHBOOK " プリセット")) {
                if (ImGui::MenuItem("デフォルト (グレー)")) {
                    gridColor_ = {0.5f, 0.5f, 0.5f, 1.0f};
                }
                if (ImGui::MenuItem("白")) {
                    gridColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
                }
                if (ImGui::MenuItem("青")) {
                    gridColor_ = {0.3f, 0.5f, 1.0f, 1.0f};
                }
                if (ImGui::MenuItem("緑")) {
                    gridColor_ = {0.3f, 1.0f, 0.5f, 1.0f};
                }
                ImGui::EndMenu();
            }

            // リセットボタン
            if (ImGui::Button(ICON_FA_UNDO " リセット")) {
                gridY_ = 0.0f;
                gridDivision_ = 10;
                gridSize_ = 1.0f;
                gridColor_ = {0.5f, 0.5f, 0.5f, 1.0f};
            }
        }

        ImGui::EndMenu();
    }
}

void ImGuiManager::DrawScreenMenuItems() {
    // 表示モード切替
    if (isShowMainUI_) {
        if (ImGui::MenuItem(ICON_FA_GAMEPAD " ゲームモードに切替", "F5")) {
            isShowMainUI_ = false;
            SwitchToGameMode();
        }
    } else {
        if (ImGui::MenuItem(ICON_FA_WRENCH " エディターモードに切替", "F5")) {
            isShowMainUI_ = true;
            SwitchToEditorMode();
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_EXPAND " フルスクリーン切替", "F11")) {
        pWinApp_->ToggleFullScreen();
    }

    // 画面解像度（ウィンドウサイズ）の変更
    // 内部レンダリングは仮想解像度固定のまま、ウィンドウと最終合成だけが変わる
    if (ImGui::BeginMenu(ICON_FA_DESKTOP " 画面解像度")) {
        struct Resolution {
            int32_t width;
            int32_t height;
        };
        static constexpr Resolution kResolutions[] = {
            {1280, 720},
            {1600, 900},
            {1760, 990},
            {1920, 1080},
        };

        const bool isFullScreen = pWinApp_->IsFullScreen();
        if (isFullScreen) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("フルスクリーン中は変更できません");
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        for (const auto &res : kResolutions) {
            std::string label = std::format("{} x {}", res.width, res.height);
            if (res.width == WinApp::GetVirtualWidth() && res.height == WinApp::GetVirtualHeight()) {
                label += " (デフォルト)";
            }
            const bool isCurrent =
                (pWinApp_->GetClientWidth() == res.width && pWinApp_->GetClientHeight() == res.height);
            if (ImGui::MenuItem(label.c_str(), nullptr, isCurrent, !isFullScreen)) {
                pWinApp_->SetClientSize(res.width, res.height);
            }
        }
        ImGui::EndMenu();
    }
}

void ImGuiManager::ShowBehaviorTreeWindow() {
    if (!showBehaviorTreeView_)
        return; // 表示しない場合は早期リターン

    // 初めて開くとき（ini に位置が残っていないとき）は、ノードを広く見られるようシーンの下を割って入れる。
    // 何もしないとメイン画面の外に置かれて別の OS ウィンドウになってしまう
    static const char *kBehaviorTreeId = "###BehaviorTreeEditor";
    // 窓ができた後は ini へ書かれるまで設定が見つからないので、窓そのものがあるかも見る（毎フレーム割らない）
    if (!ImGui::FindWindowByID(ImHashStr(kBehaviorTreeId)) && !ImGui::FindWindowSettingsByID(ImHashStr(kBehaviorTreeId))) {
        bool docked = false;
        if (dockspaceId_ != 0) {
            if (ImGuiDockNode *central = ImGui::DockBuilderGetCentralNode(dockspaceId_)) {
                ImGuiID bottomId = 0;
                ImGuiID remainingId = 0;
                ImGui::DockBuilderSplitNode(central->ID, ImGuiDir_Down, 0.45f, &bottomId, &remainingId);
                ImGui::DockBuilderDockWindow(kBehaviorTreeId, bottomId);
                ImGui::DockBuilderFinish(dockspaceId_);
                docked = true;
            }
        }
        if (!docked) {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 40.0f, viewport->WorkPos.y + viewport->WorkSize.y * 0.45f),
                                    ImGuiCond_FirstUseEver);
        }
    }
    ImGui::SetNextWindowSize(ImVec2(1200.0f, 520.0f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::Begin(ICON_FA_SITEMAP " ビヘイビアツリーエディタ###BehaviorTreeEditor", &showBehaviorTreeView_, flags);

    // シーンが持っているエディタ（敵AIなど）を集める。ファイル編集だけのエディタは除く
    std::vector<BehaviorTreeEditor *> sceneEditors;
    for (BehaviorTreeEditor *pEditor : BehaviorTreeEditor::GetInstances()) {
        if (pEditor != standaloneBtEditor_.get()) {
            sceneEditors.push_back(pEditor);
        }
    }

    if (sceneEditors.empty()) {
        // 動かす相手がいないシーンでも、ツリーのファイルは開いて直せるようにする
        if (!standaloneBtEditor_) {
            standaloneBtEditor_ = std::make_shared<BehaviorTreeEditor>();
            standaloneBtEditor_->SetDisplayName("ファイルの編集");
        }
        DimText("このシーンにはビヘイビアツリーで動くキャラクターがいません（ファイルの編集だけできます）");
        standaloneBtEditor_->OnImGuiRender();
    } else if (sceneEditors.size() == 1) {
        sceneEditors.front()->OnImGuiRender();
    } else if (ImGui::BeginTabBar("##btEditors")) {
        // 複数のキャラがツリーを持っていれば、名前のタブで切り替える
        for (size_t i = 0; i < sceneEditors.size(); ++i) {
            const std::string label = sceneEditors[i]->GetDisplayName() + "##bt" + std::to_string(i);
            if (ImGui::BeginTabItem(label.c_str())) {
                sceneEditors[i]->OnImGuiRender();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void ImGuiManager::ShowAnimStateMachineWindow() {
    // インスペクタの「エディタで開く」から頼まれたら窓ごと開く
    if (AnimationStateMachineEditor::HasOpenRequest()) {
        showAnimStateMachineView_ = true;
    }
    if (!showAnimStateMachineView_)
        return;

    // 初めて開くときはビヘイビアツリーと同じく下側へ入れる（別の OS ウィンドウにしない）
    static const char *kWindowId = "###AnimStateMachineEditor";
    if (!ImGui::FindWindowByID(ImHashStr(kWindowId)) && !ImGui::FindWindowSettingsByID(ImHashStr(kWindowId))) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 60.0f, viewport->WorkPos.y + viewport->WorkSize.y * 0.40f),
                                ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowSize(ImVec2(1200.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (AnimationStateMachineEditor::HasOpenRequest()) {
        ImGui::SetNextWindowFocus();
    }
    ImGui::Begin(ICON_FA_STREAM " アニメーションステートマシン###AnimStateMachineEditor", &showAnimStateMachineView_,
                 ImGuiWindowFlags_NoFocusOnAppearing);
    if (!animStateMachineEditor_) {
        animStateMachineEditor_ = std::make_shared<AnimationStateMachineEditor>();
    }
    animStateMachineEditor_->OnImGuiRender();
    ImGui::End();
}

void ImGuiManager::ShowSpriteManagerWindow() {
    if (!showSpriteManagerView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("スプライトマネージャ", &showSpriteManagerView_, flags);

    pSpriteManager_->DrawSpriteManager();

    ImGui::End();

    TextRenderer::GetInstance()->UpdateImGui();
}

void ImGuiManager::ShowUIEditorWindow() {
    if (!showUIEditorView_)
        return; // 表示しない場合は早期リターン

    // ウィンドウの生成・閉じるボタンは UIAnimator 側に委譲する
    UIAnimator::GetInstance()->DrawImGui(&showUIEditorView_);
}

void ImGuiManager::ShowColliderTagManagerWindow() {
    if (!showColliderTagManagerView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("コライダー", &showColliderTagManagerView_, flags);

    if (ImGui::BeginTabBar("##ColliderTabs")) {
        // コライダーの選択・サイズ調整・デバッグ表示の切り替え
        if (ImGui::BeginTabItem("コライダー設定")) {
            CollisionManager::GetInstance()->ImGuiColliderInspector();
            ImGui::EndTabItem();
        }
        // タグの追加・削除
        if (ImGui::BeginTabItem("タグ管理")) {
            CollisionManager::GetInstance()->ImGuiTagManager();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void ImGuiManager::ShowAudioManagerWindow() {
    if (!showAudioManagerView_)
        return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("オーディオ", &showAudioManagerView_, flags);

    pAudio_->Debug();

    ImGui::End();
}

void ImGuiManager::ShowCaptureWindow() {
    // ウィンドウの生成・閉じるボタンは CaptureManager 側に委譲する
    CaptureManager::GetInstance()->DrawImGui(&showCaptureView_);
}

void ImGuiManager::ShowConsoleWindow() {
    // ウィンドウの生成・閉じるボタンは DebugConsole 側に委譲する
    DebugConsole::GetInstance()->DrawImGui(&showConsoleView_);
}

void ImGuiManager::ShowTimelineWindow() {
    // ウィンドウの生成・閉じるボタンは TimelineEditor 側に委譲する
    TimelineEditor::GetInstance()->Draw(&showTimelineView_);
}

void ImGuiManager::ShowMusicEditorWindow() {
    // ウィンドウの生成・閉じるボタンは MusicEditor 側に委譲する
    // （閉じている間にPCキーボードをゲームへ返す後始末も向こうで行う）
    MusicEditor::GetInstance()->Draw(&showMusicEditorView_);
}

void ImGuiManager::ShowShadowMapWindow() {
    if (!showShadowMapView_)
        return; // 表示しない場合は早期リターン

    // ウィンドウの生成・閉じるボタンは ShadowMap 側に委譲する
    ShadowMap::GetInstance()->UpdateImGui(&showShadowMapView_);
}

void ImGuiManager::ShowCameraWindow() {
    if (!showCameraView_)
        return; // 表示しない場合は早期リターン

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::Begin("カメラ", &showCameraView_, flags);

    // カメラを置くときの2大操作: 「今シーンで見ている視点をこのカメラへ写す」「このカメラの視点からシーンを見る」
    CameraManager *cameraManager = CameraManager::GetInstance();
    Camera *selectedCamera = cameraManager->Find(cameraManager->GetSelectedName());
    DebugCamera *debugCamera = pCurrentScene_ ? pCurrentScene_->GetDebugCamera() : nullptr;
    ImGui::BeginDisabled(!selectedCamera);
    const float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ConfirmButton(ICON_FA_CAMERA " 今の視点を写す", ImVec2(halfWidth, 0.0f))) {
        const ViewProjection *viewProjection = pImGuizmoManager_->GetViewProjection();
        if (debugCamera && debugCamera->GetActive()) {
            selectedCamera->SetPosition(debugCamera->GetViewPosition());
            selectedCamera->SetRotation(debugCamera->GetViewRotation());
        } else if (viewProjection) {
            selectedCamera->SetPosition(viewProjection->translation_);
            selectedCamera->SetRotation(viewProjection->eulerRotation_);
        }
        ImGuiNotification::Post("今の視点をカメラへ写しました: " + selectedCamera->GetName(), {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::SetItemTooltip("シーンで今見ている位置と向きを、選んでいるカメラに入れます（デバッグカメラで構図を決めてから押す）");
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_EYE " ここから見る", ImVec2(halfWidth, 0.0f))) {
        if (debugCamera && pCurrentScene_) {
            if (!debugCamera->GetActive()) {
                pCurrentScene_->ToggleDebugCamera();
            }
            debugCamera->SetView(selectedCamera->GetPosition(), selectedCamera->GetRotation());
        }
    }
    ImGui::SetItemTooltip("デバッグカメラをこのカメラの位置・向きに置きます（ゲームのカメラは動かしません）");
    ImGui::EndDisabled();
    ImGui::Separator();

    cameraManager->DrawImGui();
    ImGui::End();
}

void ImGuiManager::ShowDrawSystemWindow() {
    if (!showDrawSystemView_)
        return; // 表示しない場合は早期リターン

    // ウィンドウの生成・閉じるボタンは DrawSystem 側に委譲する
    if (pDrawSystem_) {
        pDrawSystem_->UpdateImGui(&showDrawSystemView_);
    }
}

void ImGuiManager::ShowShaderEditorWindow() {
    if (!showShaderEditorView_)
        return; // 表示しない場合は早期リターン

    // ウィンドウの生成・閉じるボタンは ShaderEditorWindow 側に委譲する
    ShaderEditorWindow::GetInstance()->Draw(&showShaderEditorView_);
}

void ImGuiManager::ShowAssetBrowserWindow() {
    // 中身は EditorAssetBrowser（フォルダツリー・サムネ一覧・お気に入り・最近使った）
    if (assetBrowser_) {
        assetBrowser_->Draw(&showAssetBrowserView_);
    }
}

void ImGuiManager::ShowInspectorWindow() {
    pImGuizmoManager_->SetInspectorWindowOpen(showInspectorView_);
    if (!showInspectorView_)
        return;

    // 初めて開くとき（ini に位置が残っていないとき）は、シーンの上に浮かせて操作部品を隠さないよう、
    // 既存のオブジェクト系の窓と同じドックへ入れる。どれも無ければ画面の右端へ置く
    static const char *kInspectorId = "###Inspector";
    if (!ImGui::FindWindowSettingsByID(ImHashStr(kInspectorId))) {
        static const char *kDockBuddies[] = {"オブジェクトマネージャ", "トランスフォームマネージャ", "オブジェクト設定"};
        bool docked = false;
        for (const char *buddyName : kDockBuddies) {
            const ImGuiWindow *buddy = ImGui::FindWindowByName(buddyName);
            if (buddy && buddy->DockId != 0) {
                ImGui::SetNextWindowDockID(buddy->DockId, ImGuiCond_FirstUseEver);
                docked = true;
                break;
            }
        }
        // 相手が無ければ、シーンのある中央のドックを右へ割って細い列を作り、そこへ入れる
        if (!docked && dockspaceId_ != 0) {
            if (ImGuiDockNode *central = ImGui::DockBuilderGetCentralNode(dockspaceId_)) {
                ImGuiID rightId = 0;
                ImGuiID remainingId = 0;
                ImGui::DockBuilderSplitNode(central->ID, ImGuiDir_Right, 0.24f, &rightId, &remainingId);
                ImGui::DockBuilderDockWindow(kInspectorId, rightId);
                ImGui::DockBuilderFinish(dockspaceId_);
                docked = true;
            }
        }
        if (!docked) {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 400.0f, viewport->WorkPos.y + 80.0f),
                                    ImGuiCond_FirstUseEver);
        }
    }
    ImGui::SetNextWindowSize(ImVec2(380.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(ICON_FA_INFO_CIRCLE " インスペクタ###Inspector", &showInspectorView_, ImGuiWindowFlags_NoFocusOnAppearing)) {
        pImGuizmoManager_->DrawInspector();
    }
    ImGui::End();
}

void ImGuiManager::FixAspectRatio() {

    // 横幅ベースで16:9に合わせた高さ
    float adjustedHeight = (sceneTextureSize_.x * 9.0f / 16.0f);
    // 高さベースで16:9に合わせた横幅
    float adjustedWidth = (sceneTextureSize_.y * 16.0f / 9.0f);

    // 元のサイズとの差を計算
    float deltaFromWidth = std::abs(adjustedHeight - sceneTextureSize_.y);
    float deltaFromHeight = std::abs(adjustedWidth - sceneTextureSize_.x);

    // 近い方を採用
    if (deltaFromWidth < deltaFromHeight) {
        sceneTextureSize_.y = adjustedHeight;
    } else {
        sceneTextureSize_.x = adjustedWidth;
    }
}

void ImGuiManager::ShowSceneWindow(OffScreen *offScreen, const std::string &sceneName) {
    // ImGuiウィンドウ開始前にNextWindowSizeは設定しない（手動サイズ変更を許可）
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
    // フォーカスされていない場合は描画を最適化
    if (!isShowMainUI_) {
        flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    }
    // ウィンドウの表示名は現在のシーン名にしつつ、"###Scene" で ImGui の内部IDを固定する。
    // これにより docking レイアウトや imgui.ini の位置・サイズ設定を維持したまま、
    // タブ／タイトルの表示だけがシーンごとに切り替わる（NoTitleBar でもドッキング時のタブに表示される）。
    const std::string sceneWindowLabel = (sceneName.empty() ? std::string("Scene") : sceneName) + "###Scene";
    ImGui::Begin(sceneWindowLabel.c_str(), nullptr, flags);
    // ウィンドウ内の位置を取得（ImGuizmoのためにシーンウィンドウの絶対位置を計算）
    ImVec2 sceneWindowPos = ImGui::GetWindowPos();
    ImVec2 contentPos = ImGui::GetCursorScreenPos();

    // 以下は既存の処理を最適化
    // キャッシュされた値を使用し、毎フレーム計算しないようにする
    static ImVec2 lastContentRegion = ImVec2(0, 0);
    static ImVec2 lastSceneTextureSize = ImVec2(0, 0);

    // ウィンドウがリサイズされたか、フォーカスがあるときのみ再計算
    ImVec2 contentRegion = ImGui::GetContentRegionAvail();
    if (contentRegion.x != lastContentRegion.x ||
        contentRegion.y != lastContentRegion.y ||
        ImGui::IsWindowFocused()) {
        lastContentRegion = contentRegion;

        // 横幅ベースで16:9にしたときの高さ
        float adjustedHeight = contentRegion.x * 9.0f / 16.0f;
        // 高さベースで16:9にしたときの横幅
        float adjustedWidth = contentRegion.y * 16.0f / 9.0f;

        // 画面内に収まるように調整
        if (adjustedHeight <= contentRegion.y) {
            lastSceneTextureSize.x = contentRegion.x;
            lastSceneTextureSize.y = adjustedHeight;
        } else {
            lastSceneTextureSize.x = adjustedWidth;
            lastSceneTextureSize.y = contentRegion.y;
        }

        // 計算結果を保存
        sceneTextureSize_ = lastSceneTextureSize;
    }

    // 背景カラー設定
    static ImVec4 lastBgColor = ImVec4(0, 0, 0, 0);
    ImVec4 backgroundColor;

    // 背景色も必要時のみ更新
    if (ImGui::IsWindowFocused()) {
        backgroundColor = ImVec4(
            pDxCommon_->GetClearColor().x,
            pDxCommon_->GetClearColor().y,
            pDxCommon_->GetClearColor().z,
            pDxCommon_->GetClearColor().w);
        lastBgColor = backgroundColor;
    } else {
        backgroundColor = lastBgColor;
    }

    // シーンテクスチャの中央配置のための計算
    ImVec2 sceneOffset;
    sceneOffset.x = (contentRegion.x - sceneTextureSize_.x) * 0.5f;
    sceneOffset.y = (contentRegion.y - sceneTextureSize_.y) * 0.5f;

    // テクスチャ描画位置を調整
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + sceneOffset.x, ImGui::GetCursorPosY() + sceneOffset.y));

    // ポストエフェクトが適用された最終結果のテクスチャを取得
    uint32_t srvIndex;
    if (offScreen != nullptr) {
        // ポストエフェクトが適用された最終結果を使用
        srvIndex = offScreen->GetFinalResultSrvIndex();
    } else {
        // フォールバック：通常のオフスクリーンバッファを使用
        srvIndex = pDxCommon_->GetOffScreenSrvIndex();
    }

    // レンダーテクスチャをImGuiウィンドウに描画
    ImGui::ImageWithBg(
        static_cast<ImTextureID>(SrvManager::GetInstance()->GetGPUDescriptorHandle(srvIndex).ptr),
        sceneTextureSize_, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
        backgroundColor);

    // アセットブラウザからモデルをドラッグしてきたら、カーソルの指す位置に配置する。
    // 「生成モーダルで名前とパスを入力 → 原点に出る → 探しに行く」の往復を省くための導線。
    {
        std::string droppedModelPath;
        if (AssetDragDrop::ModelTarget(droppedModelPath)) {
            // 置いた直後にそのまま動かせるよう選択状態にする（Undo 履歴にも積まれる）
            pImGuizmoManager_->PlaceModel(droppedModelPath, pImGuizmoManager_->GetSpawnPositionUnderCursor());
        }
        // プレハブも同じくカーソルの指す位置へ
        std::string droppedPrefab;
        if (AssetDragDrop::PrefabTarget(droppedPrefab)) {
            pImGuizmoManager_->PlacePrefab(droppedPrefab, pImGuizmoManager_->GetSpawnPositionUnderCursor());
        }
    }

    // ImGuizmo 用のシーン位置（ImGui 座標系）。マルチビューポート時はスクリーン全体座標になる。
    actualScenePos_ = ImVec2(
        contentPos.x + sceneOffset.x,
        contentPos.y + sceneOffset.y);

    // レイ計算用のシーン矩形は、Mouse::GetMousePos()（仮想解像度座標）と同じ空間に合わせる。
    // ViewportsEnable 時は ImGui 座標がスクリーン全体座標になるため、まずメインビューポート位置
    // （=メインウィンドウのクライアント原点のスクリーン座標）を引いてクライアント座標へ戻し、
    // さらにマウスと同じレターボックス逆変換でクライアント座標→仮想解像度座標へ変換する。
    // これを怠ると、実ウィンドウサイズが仮想解像度と異なるときにレイの起点がずれる。
    ImVec2 mainVpPos = ImGui::GetMainViewport()->Pos;
    ImVec2 sceneClientPos = ImVec2(actualScenePos_.x - mainVpPos.x, actualScenePos_.y - mainVpPos.y);

    float viewX = 0.0f, viewY = 0.0f, viewW = 0.0f, viewH = 0.0f;
    WinApp::ComputeLetterboxRect(pWinApp_->GetClientWidth(), pWinApp_->GetClientHeight(), viewX, viewY, viewW, viewH);
    const float toVirtualX = static_cast<float>(WinApp::GetVirtualWidth()) / viewW;
    const float toVirtualY = static_cast<float>(WinApp::GetVirtualHeight()) / viewH;
    scenePosForRay_ = ImVec2((sceneClientPos.x - viewX) * toVirtualX, (sceneClientPos.y - viewY) * toVirtualY);
    sceneSizeForRay_ = ImVec2(sceneTextureSize_.x * toVirtualX, sceneTextureSize_.y * toVirtualY);

    // 他の ImGui ウィンドウがシーンウィンドウの上に重なっているとき、その上でのクリックで
    // シーンのオブジェクト選択を誤発火させないよう、シーンウィンドウのホバー状態を渡す。
    bool sceneHovered = ImGui::IsWindowHovered();
    // ライト・カメラ等のアイコンのクリックは、ギズモのクリック選択より先に処理する（押したら奥の物は選ばない）
    UpdateSceneIcons(actualScenePos_, sceneTextureSize_, sceneHovered);
    pImGuizmoManager_->Update(actualScenePos_, sceneTextureSize_, sceneHovered);
    DrawSceneIcons(actualScenePos_, sceneTextureSize_);

    // ツールバー・軸の向き表示（子ウィンドウなので、上にマウスがある間はシーンの選択が発火しない）
    overlaySceneName_ = sceneName;
    HandleCameraBookmarkKeys(sceneHovered);
    DrawSceneOverlay(actualScenePos_, sceneTextureSize_);
    DrawSceneContextMenu(sceneHovered);

    ImGui::End();
}

void ImGuiManager::ShowMainUI(OffScreen *pOffScreen) {

    // ヒエラルキーウィンドウ
    ShowSceneSettingWindow();
    // インスペクターウィンドウ
    ShowObjectSettingWindow();
    // プロジェクトウィンドウを描画
    ShowParticleSettingWindow();
    // GPUパーティクル プレビュー窓を描画
    ShowParticlePreviewWindow();
    // FPSを描画
    ShowStatisticsWindow();
    // オフスクリーンウィンドウを描画
    ShowOffScreenSettingWindow(pOffScreen);
    // ライトウィンドウを描画
    ShowLightSettingWindow();
    // ギズモウィンドウを描画
    ShowGizmoWindow();
    // インスペクタ窓を描画
    ShowInspectorWindow();
    // 操作の履歴窓を描画
    ShowUndoHistoryWindow();
    // 配置ツール窓を描画
    ShowPlacementToolWindow();
    // カラーパレット窓を描画
    ShowColorPaletteWindow();
    // 階層エディターウィンドウを描画
    ShowHierarchyWindow();
    // モーションエディターウィンドウを描画
    ShowMotionEditorWindow();
    // ビヘイビアツリーエディタウィンドウを描画
    ShowBehaviorTreeWindow();
    // アニメーションのステートマシンの窓を描画
    ShowAnimStateMachineWindow();
    // カメラビュー窓（好きなカメラから見たシーン）を描画
    SceneViewRenderer::GetInstance()->DrawImGui();
    // スプライトマネージャウィンドウを描画
    ShowSpriteManagerWindow();
    // UIエディタウィンドウを描画
    ShowUIEditorWindow();
    // コライダータグマネージャウィンドウを描画
    ShowColliderTagManagerWindow();
    // オーディオマネージャウィンドウを描画
    ShowAudioManagerWindow();
    // 音楽エディタウィンドウを描画
    ShowMusicEditorWindow();
    // キャプチャウィンドウを描画
    ShowCaptureWindow();
    // コンソールウィンドウを描画
    ShowConsoleWindow();
    // タイムラインウィンドウを描画
    ShowTimelineWindow();
    // シャドウマップ設定ウィンドウを描画
    ShowShadowMapWindow();
    // 描画システム設定ウィンドウを描画
    ShowDrawSystemWindow();
    ShowShaderEditorWindow();
    // カメラ窓を描画
    ShowCameraWindow();
    // アセットブラウザ窓を描画
    ShowAssetBrowserWindow();
    // ゲームパラメータHub窓を描画
    ShowGameParamWindow();

    ShowHelpWindow();
    // 通知の履歴・外観の設定窓
    ShowNotificationWindow();
    ShowAppearanceWindow();
    // コマンドパレット（Ctrl+K）。ゲームモード中でも呼び出せる
    ShowCommandPalette();
    // プレハブ保存などのダイアログ（どの窓から開いても出るように毎フレーム）
    pImGuizmoManager_->DrawEditorModals();
    // 画像のホットリロード（アセットブラウザが閉じていても見張る）
    if (assetBrowser_) {
        assetBrowser_->PollFileChanges();
    }
    pBaseObjectManager_->UpdateImGui();
    // シーンの保存・読み込みダイアログ（メニュー・ショートカット・コマンドパレットのどこから開いても出る）
    SceneSerializer::GetInstance()->DrawImGui();
    pSpriteManager_->UpdateImGui();
    // 光源はライト設定ウィンドウを閉じていてもギズモで掴めるので、
    // 追跡はウィンドウの表示状態と切り離してここで回す
    LightGroup::GetInstance()->UpdateImGui();
}

bool &ImGuiManager::GetIsShowMainUI() {
    return isShowMainUI_;
}

void ImGuiManager::ShowDockSpace() {
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("DockSpaceWindow", nullptr, window_flags);
    ImGui::PopStyleVar(2);

    // DockSpaceの生成
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    dockspaceId_ = dockspace_id;
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

#endif // USE_IMGUI

void ImGuiManager::DisplayFPS() {
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("FPS")) {

        // 履歴サイズ（フレーム数）
        static const int kHistSize = 256;

        // 循環バッファ
        static float fpsHistory[kHistSize] = {};
        static float frameTimeHistory[kHistSize] = {};
        static int offset = 0;
        static float fpsMax = 70.0f; // Y軸上限（適宜調整）

        // 今フレームの値を記録
        float fps = Frame::GetFPS();
        float frameTime = Frame::UnscaledDeltaTime() * 1000.0f; // ms

        fpsHistory[offset] = fps;
        frameTimeHistory[offset] = frameTime;
        offset = (offset + 1) % kHistSize;

        // 色判定（数値表示用）
        ImVec4 color =
            fps >= 59.0f ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : fps >= 30.0f ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f)
                                                                         : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

        // 数値をコンパクトに横並び表示（列位置は文字サイズ基準。px 直書きだと
        // フォントを変えたときに "FPS: 60.0" と重なる）
        const float fpsColX = ImGui::GetCursorPosX();
        ImGui::TextColored(color, "FPS: %.1f", fps);
        ImGui::SameLine(fpsColX + LabelColumnWidth());
        ImGui::TextColored(color, "Frame: %.2f ms", frameTime);

        // -----------------------------------------------
        // FPS 折れ線グラフ
        // -----------------------------------------------
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
        ImGui::TextUnformatted("FPS 履歴");
        ImGui::PopStyleColor();

        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.05f, 0.05f, 0.09f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.45f, 0.65f, 0.85f, 1.0f));

        if (ImPlot::BeginPlot("##FPSPlot", ImVec2(-1, 90),
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoInputs |
                                  ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
            ImPlot::SetupAxes(nullptr, "FPS",
                              ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines,
                              ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHistSize, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, fpsMax, ImPlotCond_Always);

            // 60fps / 30fps の基準線
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.0f, 1.0f, 0.0f, 0.4f));
            double y60 = 60.0;
            ImPlot::PlotInfLines("##60", &y60, 1, ImPlotInfLinesFlags_Horizontal);
            ImPlot::PopStyleColor();

            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 1.0f, 0.0f, 0.4f));
            double y30 = 30.0;
            ImPlot::PlotInfLines("##30", &y30, 1, ImPlotInfLinesFlags_Horizontal);
            ImPlot::PopStyleColor();

            // FPS折れ線
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.45f, 0.65f, 0.85f, 1.0f));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
            ImPlot::PlotLine("FPS", fpsHistory, kHistSize, 1.0, 0.0,
                             ImPlotLineFlags_None, offset);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();

            ImPlot::EndPlot();
        }

        ImPlot::PopStyleColor(3);

        // -----------------------------------------------
        // フレームタイム 折れ線グラフ
        // -----------------------------------------------
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
        ImGui::TextUnformatted("フレームタイム 履歴 (ms)");
        ImGui::PopStyleColor();

        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.05f, 0.05f, 0.09f, 1.0f));

        if (ImPlot::BeginPlot("##FrameTimePlot", ImVec2(-1, 70),
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoInputs |
                                  ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
            ImPlot::SetupAxes(nullptr, "ms",
                              ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines,
                              ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHistSize, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 50.0, ImPlotCond_Always);

            // 16.6ms（60fps相当）の基準線
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.0f, 1.0f, 0.0f, 0.4f));
            double y16 = 16.6;
            ImPlot::PlotInfLines("##16ms", &y16, 1, ImPlotInfLinesFlags_Horizontal);
            ImPlot::PopStyleColor();

            // フレームタイム折れ線（遅いほど赤寄り）
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
            ImPlot::PlotLine("ms", frameTimeHistory, kHistSize, 1.0, 0.0,
                             ImPlotLineFlags_None, offset);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();

            ImPlot::EndPlot();
        }

        ImPlot::PopStyleColor(2);
    }
#endif // USE_IMGUI
}

// 現在のDockレイアウトを保存
void ImGuiManager::BackupDockLayout() {
#ifdef USE_IMGUI
    ImGuiContext *context = ImGui::GetCurrentContext();
    if (context) {
        // Dockingレイアウトを文字列として保存
        dockLayoutBackup_ = ImGui::SaveIniSettingsToMemory();
    }
#endif // USE_IMGUI
}

// 保存したレイアウトを復元
void ImGuiManager::RestoreDockLayout() {
#ifdef USE_IMGUI
    if (!dockLayoutBackup_.empty()) {
        // メモリ上の設定を再適用
        ImGui::LoadIniSettingsFromMemory(dockLayoutBackup_.c_str(), dockLayoutBackup_.size());
    }
#endif // USE_IMGUI
}

void ImGuiManager::SwitchToEditorMode() {
    if (!isEditorMode_) {
        // ゲームモードからエディターモードへの切替
        SaveCurrentLayout(); // 現在のゲームモードレイアウトを保存
        isEditorMode_ = true;
        LoadLayoutForCurrentMode(); // エディターモードのレイアウトをロード
#ifdef USE_IMGUI
        ImGuiNotification::Post("エディターモードに切り替えました", {0.4f, 0.8f, 1.0f, 1.0f});
#endif // USE_IMGUI
    }
}

void ImGuiManager::SwitchToGameMode() {
    if (isEditorMode_) {
        // エディターモードからゲームモードへの切替
        SaveCurrentLayout(); // 現在のエディターモードレイアウトを保存
        isEditorMode_ = false;
        LoadLayoutForCurrentMode(); // ゲームモードのレイアウトをロード
#ifdef USE_IMGUI
        ImGuiNotification::Post("ゲームモードに切り替えました", {0.4f, 0.8f, 1.0f, 1.0f});
#endif // USE_IMGUI
    }
}

// ゲームモードini用: [Docking]セクション全体と各[Window]のDockId行を除去する
// F5でアンドックしたウィンドウのノードIDが次回起動時に存在せず
// imgui.cpp の "node != 0" アサーションを引き起こすのを防ぐ
#ifdef USE_IMGUI
static std::string StripDockDataFromIni(const char *src, size_t srcSize) {
    std::string result;
    result.reserve(srcSize);
    size_t pos = 0;
    bool skipSection = false;

    while (pos < srcSize) {
        size_t lineEnd = pos;
        while (lineEnd < srcSize && src[lineEnd] != '\n')
            ++lineEnd;
        // pLine は src[pos..lineEnd) ('\n' を含まない)
        const char *linePtr = src + pos;
        size_t lineLen = lineEnd - pos;
        pos = lineEnd + 1; // 次の行へ

        // セクションヘッダ判定
        if (lineLen > 0 && linePtr[0] == '[') {
            // [Docking] セクションはスキップフラグを立てる
            bool isDocking = (lineLen >= 9 &&
                              linePtr[1] == 'D' && linePtr[2] == 'o' &&
                              linePtr[3] == 'c' && linePtr[4] == 'k' &&
                              linePtr[5] == 'i' && linePtr[6] == 'n' &&
                              linePtr[7] == 'g' && linePtr[8] == ']');
            skipSection = isDocking;
            if (isDocking)
                continue;
        }

        if (skipSection)
            continue;

        // DockId= 行はスキップ（ウィンドウの古いノード参照を除去）
        if (lineLen >= 7 &&
            linePtr[0] == 'D' && linePtr[1] == 'o' && linePtr[2] == 'c' &&
            linePtr[3] == 'k' && linePtr[4] == 'I' && linePtr[5] == 'd' &&
            linePtr[6] == '=') {
            continue;
        }

        result.append(linePtr, lineLen);
        result += '\n';
    }
    return result;
}
#endif // USE_IMGUI

void ImGuiManager::SaveCurrentLayout() {
#ifdef USE_IMGUI
    // 現在のモードに応じたファイルにレイアウトを保存
    const char *iniFilePath = isEditorMode_ ? editorIniFilePath_.c_str() : gameIniFilePath_.c_str();

    // 保存先フォルダ(AssetPath::ConfigRoot())が無ければ作成する（fopen はディレクトリを作らないため）。
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(iniFilePath).parent_path(), ec);

    // メモリからiniデータを取得
    size_t size = 0;
    const char *iniData = ImGui::SaveIniSettingsToMemory(&size);

    // ファイルに書き込み
    FILE *f = nullptr;
    if (fopen_s(&f, iniFilePath, "wt") == 0 && f) {
        if (!isEditorMode_) {
            // ゲームモード: ドックノード参照を除去して保存
            std::string stripped = StripDockDataFromIni(iniData, size);
            fwrite(stripped.c_str(), sizeof(char), stripped.size(), f);
        } else {
            fwrite(iniData, sizeof(char), size, f);
        }
        fclose(f);
    }
    ImGuiNotification::Post("レイアウトを保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
#endif // USE_IMGUI
}

void ImGuiManager::LoadLayoutForCurrentMode() {
#ifdef USE_IMGUI
    // モードに応じたiniファイルをロード
    const char *iniFilePath = isEditorMode_ ? editorIniFilePath_.c_str() : gameIniFilePath_.c_str();

    // ファイルが存在する場合はロード
    FILE *f = nullptr;
    if (fopen_s(&f, iniFilePath, "rt") == 0 && f) {
        // ファイルサイズを取得
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);

        // バッファを確保してファイル内容を読み込む
        char *buf = new char[size + 1];
        if (buf) {
            size_t read_size = fread(buf, 1, size, f);
            buf[read_size] = 0;

            if (!isEditorMode_) {
                // ゲームモード: 古いドックノード参照を除去してからロード
                std::string stripped = StripDockDataFromIni(buf, read_size);
                ImGui::LoadIniSettingsFromMemory(stripped.c_str(), stripped.size());
            } else {
                ImGui::LoadIniSettingsFromMemory(buf, read_size);
            }

            delete[] buf;
        }
        fclose(f);
    }
    // ファイルが存在しない場合は新規に作成される
#endif // USE_IMGUI
}

void ImGuiManager::ShowHelpWindow() {
#ifdef USE_IMGUI

    // ショートカット一覧ウィンドウの表示
    if (showShortcutWindow_) {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(ICON_FA_KEYBOARD " ショートカット一覧", &showShortcutWindow_, ImGuiWindowFlags_NoCollapse)) {

            // テーブルでショートカットを整理して表示
            if (ImGui::BeginTable("ShortcutTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("機能", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                ImGui::TableSetupColumn("ショートカットキー", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableHeadersRow();

                // システム操作
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_COG " システム操作");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  ショートカット一覧（この画面）");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("F1");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  再生 / 一時停止");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + P");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  停止（再生前の状態へ戻す）");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Shift + P");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  デバッグカメラ切り替え");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("F3");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  アプリケーション終了");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Alt + F4");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  デバッグUI切り替え");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("F5");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  フルスクリーン切り替え");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("F11");

                // シーン操作
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_FOLDER " シーン操作");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  シーン保存（上書き）");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + S");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  名前を付けて保存");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Shift + S");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  シーン読み込み");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Shift + L");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  モデル作成");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Shift + N");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  保存済みオブジェクト読み込み");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Shift + M");

                // シーン切り替え
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_EXCHANGE_ALT " シーン切り替え");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");

                // シーン名を直書きすると増減のたびに実際の割り当てとずれるので、
                // Framework::RegisterShortcutKey と同じ SceneRegistry の順番から生成する
                {
                    const std::vector<std::string> registeredScenes = SceneRegistry::GetInstance()->GetSceneNames();
                    for (size_t i = 0; i < registeredScenes.size() && i < 9; ++i) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("  %s", registeredScenes[i].c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("Ctrl + %zu", i + 1);
                    }
                }

                // オブジェクト操作
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_CUBES " 選択オブジェクト操作");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  元に戻す");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Z");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  やり直す");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + Y");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  オブジェクトコピー");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + C");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  オブジェクトペースト");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + V");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  オブジェクト複製");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Ctrl + D");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  オブジェクト削除");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Delete");

                // ギズモ操作（シーンウィンドウにマウスがある間のみ有効）
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_ARROWS_ALT " ギズモ操作 (シーン上)");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");

                struct GizmoShortcutRow {
                    const char *label;
                    const char *key;
                };
                static const GizmoShortcutRow kGizmoShortcuts[] = {
                    {"  移動 / 回転 / スケール", "1 / 2 / 3"},
                    {"  ローカル ⇔ ワールド", "4"},
                    {"  スナップ ON/OFF", "5"},
                    {"  スナップを一時反転", "Shift (押している間)"},
                    {"  選択オブジェクトへ寄る", "F"},
                    {"  重なった物を順に選択", "Tab"},
                    {"  矩形選択（Ctrlで追加選択）", "空きスペースをドラッグ"},
                    {"  その場所に置く・選択の操作・視点", "右クリック（動かさずに離す）"},
                    {"  カメラのブックマークへ移動", "Shift + 1〜9"},
                    {"  カメラのブックマークに保存", "Ctrl + Shift + 1〜9"},
                    {"  クリック対象をその種類だけに", "Alt + 1〜4"},
                    {"  クリック対象をすべてに", "Alt + 0"},
                };
                for (const GizmoShortcutRow &row : kGizmoShortcuts) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(row.label);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(row.key);
                }

                // エディタの窓
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), ICON_FA_WINDOW_RESTORE " エディタの窓");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("");
                static const GizmoShortcutRow kEditorShortcuts[] = {
                    {"  コマンドパレット（$ でアセット）", "Ctrl + K"},
                    {"  階層: 範囲選択 / 追加選択", "Shift / Ctrl + クリック"},
                    {"  階層: 選択を1行ずつ動かす", "↑ ↓（Shift で範囲）"},
                    {"  数値欄のコピー・貼り付け・既定値", "数値欄を右クリック"},
                    {"  アセットブラウザ: サムネの大きさ", "Ctrl + ホイール"},
                    {"  操作の履歴を開く", "ステータスバーの Undo 表示をクリック"},
                };
                for (const GizmoShortcutRow &row : kEditorShortcuts) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(row.label);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(row.key);
                }

                ImGui::EndTable();
            }

            // 注記
            ImGui::Separator();
            ImGui::TextWrapped("注意: これらのショートカットはデバッグビルドでのみ有効です。");

            // 閉じるボタン
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 35);
            if (ImGui::Button("閉じる", ImVec2(100, 25))) {
                showShortcutWindow_ = false;
            }
        }
        ImGui::End();
    }
#endif // USE_IMGUI
}

void ImGuiManager::SaveFlag() {
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ImGuiSetting", "Frags");
    data->Save("IsShowMainUI", isShowMainUI_);
    data->Save("ShowGrid", showGrid_);
    data->Save("showSceneView", showSceneView_);
    data->Save("showObjectView", showObjectView_);
    data->Save("showParticleView", showParticleView_);
    data->Save("showParticlePreviewView", showParticlePreviewView_);
    data->Save("showFPSView", showFPSView_);
    data->Save("showOfScreenView", showOfScreenView_);
    data->Save("showLightView", showLightView_);
    data->Save("showGizmoView", showGizmoView_);
    data->Save("showHierarchyView", showHierarchyView_);
    data->Save("showMotionEditorView", showMotionEditorView_);
    data->Save("showBehaviorTreeView", showBehaviorTreeView_);
    data->Save("showAnimStateMachineView", showAnimStateMachineView_);
    data->Save("showShortcutWindow", showShortcutWindow_);
    data->Save("showSpriteManagerView", showSpriteManagerView_);
    data->Save("showUIEditorView", showUIEditorView_);
    data->Save("showShadowMapView", showShadowMapView_);
    data->Save("showDrawSystemView", showDrawSystemView_);
    data->Save("showShaderEditorView", showShaderEditorView_);
    data->Save("showMusicEditorView", showMusicEditorView_);
    data->Save("showCaptureView", showCaptureView_);
    data->Save("showConsoleView", showConsoleView_);
    data->Save("showTimelineView", showTimelineView_);
    data->Save("showAssetBrowserView", showAssetBrowserView_);
    data->Save("showGameParamView", showGameParamView_);
    data->Save("showColliderTagManagerView", showColliderTagManagerView_);
    data->Save("showAudioManagerView", showAudioManagerView_);
    data->Save("showCameraView", showCameraView_);
    data->Save("showNotificationView", showNotificationView_);
    data->Save("showAppearanceView", showAppearanceView_);
    data->Save("showInspectorView", showInspectorView_);
    data->Save("showUndoHistoryView", showUndoHistoryView_);
    data->Save("showPlacementToolView", showPlacementToolView_);
    data->Save("showColorPaletteView", showColorPaletteView_);
    data->Save("sceneLabelMode", sceneLabelMode_);
    data->Save("showSceneMeasure", showSceneMeasure_);
    data->Save("showThirdsGuide", showThirdsGuide_);
    data->Save("showSafeAreaGuide", showSafeAreaGuide_);
    data->Save("showSceneOverlay", showSceneOverlay_);
    data->Save("showViewAxis", showViewAxis_);
    data->Save("showSceneIcons", showSceneIcons_);
    data->Save("sceneIconScale", sceneIconScale_);
    data->Save("sceneIconKindMask", sceneIconKindMask_);
#ifdef USE_IMGUI
    data->Save("currentWorkspace", currentWorkspace_);
    {
        // シーンのクリック対象（今のワークスペースのぶんは今の状態で上書きしてから保存）
        ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
        if (currentWorkspace_ >= 0 && currentWorkspace_ < static_cast<int>(workspacePickMasks_.size()))
        {
            workspacePickMasks_[currentWorkspace_] = gizmo->GetCategoryMask();
        }
        data->Save("gizmoPickMask", static_cast<int>(gizmo->GetCategoryMask()));
        for (size_t i = 0; i < workspacePickMasks_.size(); ++i)
        {
            data->Save("gizmoPickMask_" + std::to_string(i), static_cast<int>(workspacePickMasks_[i]));
        }
    }
#endif // USE_IMGUI
    data->Save("isEditorMode", isEditorMode_);
    data->Save("gridColor", gridColor_);
#ifdef USE_IMGUI
    ImGuiNotification::Post("UI設定を保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
#endif // USE_IMGUI
}

void ImGuiManager::LoadFlag() {
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ImGuiSetting", "Frags");
    isShowMainUI_ = data->Load("IsShowMainUI", true);
    showGrid_ = data->Load("ShowGrid", true);
    showSceneView_ = data->Load("showSceneView", true);
    showObjectView_ = data->Load("showObjectView", true);
    showParticleView_ = data->Load("showParticleView", false);
    showParticlePreviewView_ = data->Load("showParticlePreviewView", false);
    showFPSView_ = data->Load("showFPSView", true);
    showOfScreenView_ = data->Load("showOfScreenView", false);
    showLightView_ = data->Load("showLightView", false);
    showGizmoView_ = data->Load("showGizmoView", false);
    showHierarchyView_ = data->Load("showHierarchyView", true);
    showMotionEditorView_ = data->Load("showMotionEditorView", false);
    showBehaviorTreeView_ = data->Load("showBehaviorTreeView", false);
    showAnimStateMachineView_ = data->Load("showAnimStateMachineView", false);
    showShortcutWindow_ = data->Load("showShortcutWindow", false);
    showSpriteManagerView_ = data->Load("showSpriteManagerView", false);
    showUIEditorView_ = data->Load("showUIEditorView", false);
    showShadowMapView_ = data->Load("showShadowMapView", true);
    showDrawSystemView_ = data->Load("showDrawSystemView", true);
    showShaderEditorView_ = data->Load("showShaderEditorView", false);
    showMusicEditorView_ = data->Load("showMusicEditorView", false);
    showCaptureView_ = data->Load("showCaptureView", false);
    showConsoleView_ = data->Load("showConsoleView", false);
    showTimelineView_ = data->Load("showTimelineView", false);
    showAssetBrowserView_ = data->Load("showAssetBrowserView", false);
    showGameParamView_ = data->Load("showGameParamView", true);
    showColliderTagManagerView_ = data->Load("showColliderTagManagerView", false);
    showAudioManagerView_ = data->Load("showAudioManagerView", false);
    showCameraView_ = data->Load("showCameraView", false);
    showNotificationView_ = data->Load("showNotificationView", false);
    showAppearanceView_ = data->Load("showAppearanceView", false);
    showInspectorView_ = data->Load("showInspectorView", true);
    showUndoHistoryView_ = data->Load("showUndoHistoryView", false);
    showPlacementToolView_ = data->Load("showPlacementToolView", false);
    showColorPaletteView_ = data->Load("showColorPaletteView", false);
    sceneLabelMode_ = data->Load("sceneLabelMode", 1);
    showSceneMeasure_ = data->Load("showSceneMeasure", true);
    showThirdsGuide_ = data->Load("showThirdsGuide", false);
    showSafeAreaGuide_ = data->Load("showSafeAreaGuide", false);
    showSceneOverlay_ = data->Load("showSceneOverlay", true);
    showViewAxis_ = data->Load("showViewAxis", true);
    showSceneIcons_ = data->Load("showSceneIcons", true);
    sceneIconScale_ = data->Load("sceneIconScale", 1.0f);
    sceneIconKindMask_ = data->Load("sceneIconKindMask", 0x1F);
#ifdef USE_IMGUI
    currentWorkspace_ = data->Load("currentWorkspace", -1);
    {
        ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
        for (size_t i = 0; i < workspacePickMasks_.size(); ++i)
        {
            workspacePickMasks_[i] = static_cast<uint32_t>(
                data->Load("gizmoPickMask_" + std::to_string(i), static_cast<int>(workspacePickMasks_[i])));
        }
        gizmo->SetCategoryMask(static_cast<uint32_t>(data->Load("gizmoPickMask", static_cast<int>(gizmo->GetCategoryMask()))));
    }
#endif // USE_IMGUI
    isEditorMode_ = data->Load("isEditorMode", true);
    gridColor_ = data->Load("gridColor", Vector4(0.5f, 0.5f, 0.5f, 1.0f));
}
} // namespace Hagine
