#pragma once
#include "Asset/AssetPath.h"
#include "DirectXCommon.h"
#include "SpriteManager.h"
#include "WinApp.h"
#include "object/base/BaseObjectManager.h"
#include <Audio.h>
#include <BaseScene.h>
#include <array>
#include <map>
#include <memory>
#include <vector>
#include "EditorAppearance.h"
#include "EditorCommandPalette.h"
#include "EditorAssetBrowser.h"

namespace Hagine {
class ImGuizmoManager;
class BehaviorTreeEditor;
class AnimationStateMachineEditor;
class OffScreen;
class DrawSystem;
class ImGuiManager {
  private:
    ImGuizmoManager *pImGuizmoManager_ = nullptr;
    WinApp *pWinApp_ = nullptr;
    DrawSystem *pDrawSystem_ = nullptr;

  public:
    /// ====================================
    /// public method
    /// ====================================

    ImGuiManager() = default;
    ~ImGuiManager() = default;
    ImGuiManager(ImGuiManager &) = delete;
    ImGuiManager &operator=(ImGuiManager &) = delete;

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(WinApp *winApp, ImGuizmoManager *imguizmoManager);

    void SetupTheme();

    /// <summary>
    /// ImPlot 側の配色を ImGui のテーマへ合わせる（SetupTheme から呼ばれる）
    /// </summary>
    void SetupPlotTheme();

    /// <summary>
    /// 統計ウィンドウで参照する DrawSystem を設定（Framework が注入する）
    /// </summary>
    void SetDrawSystem(DrawSystem *drawSystem) { pDrawSystem_ = drawSystem; }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// ImGui受付開始
    /// </summary>
    void Begin();

    /// <summary>
    /// ImGui受付終了
    /// </summary>
    void End();

    /// <summary>
    /// 画面への描画
    /// </summary>
    void Draw();

    /// <summary>
    /// マルチビューポート描画（ドックから切り離したウィンドウを独立OSウィンドウとして描画）。
    /// メインビューポートの Present 後に呼ぶこと。
    /// </summary>
    void RenderMultiViewport();

    /// <summary>
    /// .iniファイル関連の更新
    /// </summary>
    void UpdateIni();

    /// <summary>
    /// メインUI表示
    /// </summary>
    void ShowMainUI(OffScreen *pOffScreen);

    /// <summary>
    /// メニュー表示
    /// </summary>
    void ShowMainMenu();

    /// <summary>
    /// ドックスペース追加
    /// </summary>
    void ShowDockSpace();

    void DisplayFPS();

    bool &GetIsShowMainUI();
    void SetCurrentScene(BaseScene *currentScene) { pCurrentScene_ = currentScene; };

    void SetImGuizmoManager(ImGuizmoManager *manager) {
        pImGuizmoManager_ = manager;
    }

    void SetShortcutWindow(bool show) {
        showShortcutWindow_ = show;
    }

    // 必要に応じてImGuizmoManagerへのアクセサを追加
    ImGuizmoManager *GetImGuizmoManager() const {
        return pImGuizmoManager_;
    }

    /// <summary>
    /// シーン表示
    /// </summary>
    void ShowSceneWindow(OffScreen *offScreen, const std::string &sceneName);
#ifdef USE_IMGUI
    // レイ計算用のシーン矩形（仮想解像度座標系。Mouse::GetMousePos と同じ空間）。
    // ImGui 座標のシーン矩形をマウスと同じレターボックス逆変換に通してあり、
    // ウィンドウサイズ変更・マルチビューポート時もレイ計算と整合する。
    Vector2 GetScenePosForRay() const {
        return Vector2(scenePosForRay_.x, scenePosForRay_.y);
    }
    Vector2 GetSceneSizeForRay() const {
        return Vector2(sceneSizeForRay_.x, sceneSizeForRay_.y);
    }
#endif // USE_IMGUI

    bool GetEditorMode() const {
        return isEditorMode_;
    }

  private:
    /// ====================================
    /// private method
    /// ====================================

    /// <summary>
    /// ヒープ作成
    /// </summary>
    void CreateDescriptorHeap();

    /// <summary>
    /// ヒエラルキー表示
    /// </summary>
    void ShowSceneSettingWindow();

    void ShowObjectSettingWindow();

    void ShowParticleSettingWindow();

    // GPUパーティクルのプレビュー窓（表示メニューでON/OFF）
    void ShowParticlePreviewWindow();

    void ShowStatisticsWindow();

    void ShowOffScreenSettingWindow(OffScreen *pOffScreen);

    void ShowLightSettingWindow();

    void ShowGizmoWindow();

    void ShowHierarchyWindow();

    void ShowMotionEditorWindow();

    void ShowBehaviorTreeWindow();
    void ShowAnimStateMachineWindow();

    void ShowSpriteManagerWindow();

    // UIエディタ窓（スプライトのグループ管理＋名前付きトゥイーン）
    void ShowUIEditorWindow();

    void ShowColliderTagManagerWindow();

    void ShowAudioManagerWindow();

    // 音楽エディタ窓（鍵盤演奏・ピアノロール打ち込み・音色作り・wav 加工と書き出し）
    void ShowMusicEditorWindow();

    // キャプチャ窓（スクリーンショットと連番録画）
    void ShowCaptureWindow();

    // コンソール窓（実行中にコマンドを打って値を見る・変える）
    void ShowConsoleWindow();

    // タイムライン窓（カメラ・オブジェクト・音・パーティクルの演出を時間軸で組む）
    void ShowTimelineWindow();

    void ShowShadowMapWindow();

    void ShowDrawSystemWindow();

    // シェーダー窓（shaders/ 配下のHLSLを構文色付きで閲覧・編集する）
    void ShowShaderEditorWindow();

    // カメラ窓（登録カメラの一覧・切り替え・各カメラの設定）
    void ShowCameraWindow();

    // デバッグカメラ窓（使う/使わない・位置と向き・動かし方）
    void ShowDebugCameraWindow();

    // デバッグ線の窓（線の種類ごとの本数とオン/オフ）
    void ShowDebugLineWindow();

    // 入力（キーコンフィグ）の窓
    void ShowInputActionWindow();

    // カメラ窓の頭に出すデバッグカメラの状態（使う/使わない・速さ・窓を開く）
    void DrawDebugCameraSummary();

    // アセットブラウザ窓（images ルートをサムネ一覧表示、各サムネをD&Dのドラッグ元にする）
    void ShowAssetBrowserWindow();

    // インスペクタ窓（選択中の物の詳細・複数選択のまとめて編集）
    void ShowInspectorWindow();

#ifdef USE_IMGUI
    /// <summary>
    /// シーンビューの上に重ねるツールバー・軸の向き表示・再生状態の枠（ImGuiManagerSceneOverlay.cpp）
    /// </summary>
    /// <param name="imageMin">シーン画像の左上（ImGui 座標）</param>
    /// <param name="imageSize">シーン画像の大きさ</param>
    void DrawSceneOverlay(const ImVec2 &imageMin, const ImVec2 &imageSize);

    /// <summary>シーンビューの右クリックメニュー（右ドラッグは視点の回転なので、動かさずに離したときだけ開く）</summary>
    void DrawSceneContextMenu(bool sceneHovered);
    Vector3 sceneContextPosition_ = {0.0f, 0.0f, 0.0f}; // 右クリックした場所（置くときの位置）

    /// <summary>シーンビューに名前ラベルと距離の計測を描く</summary>
    void DrawSceneLabels(const ImVec2 &imageMin, const ImVec2 &imageSize);

    // ---- シーンのアイコン（ImGuiManagerSceneIcons.cpp）----
    // ライト・カメラ・パーティクルのエミッター・フィールドを、画面上で常に同じ大きさのアイコンで出す。
    // 押すとその物を選び、選択中の物は影響範囲（球・円錐・視錐台など）を線で描く。

    /// <summary>アイコンの種類（表示の ON/OFF のビット番号も兼ねる）</summary>
    enum class SceneIconKind
    {
        PointLight = 0,
        SpotLight = 1,
        Camera = 2,
        Emitter = 3,
        Field = 4,
    };
    static constexpr int kSceneIconKindCount = 5;

    /// <summary>このフレームに描くアイコン1個ぶん</summary>
    struct SceneIcon
    {
        SceneIconKind kind = SceneIconKind::PointLight;
        Vector3 position = {0.0f, 0.0f, 0.0f};
        ImVec2 screen = {0.0f, 0.0f};
        float radius = 12.0f;  // 画面上の半径（ピクセル）
        float alpha = 1.0f;    // 遠いほど薄く
        float depth = 0.0f;    // カメラからの距離（手前を上に描く・当たりを優先する）
        Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        Vector3 direction = {0.0f, 0.0f, 0.0f}; // 向きのあるもの（スポット・カメラ）は縁に向きの印を出す
        const char *glyph = "";
        std::string gizmoName;  // 選ぶときのギズモ登録名（カメラは空）
        std::string cameraName; // カメラのときの名前
        std::string label;      // 選択中・ホバー中に出す名前
        bool selected = false;
        bool pickable = true;   // クリック対象フィルタで外した種類は押せない
        int index = -1;         // ライト・フィールドの番号（範囲の線を引くとき用）
    };

    /// <summary>アイコンを集めて画面位置を求め、クリックを処理する（ギズモの Update より先に呼ぶ）</summary>
    void UpdateSceneIcons(const ImVec2 &imageMin, const ImVec2 &imageSize, bool sceneHovered);

    /// <summary>集めたアイコンを描く（DrawSceneOverlay から）</summary>
    void DrawSceneIcons(const ImVec2 &imageMin, const ImVec2 &imageSize);

    /// <summary>選択中のライト・カメラ・フィールドの範囲を線で描く</summary>
    void DrawSelectedIconRanges();

    /// <summary>ツールバーのアイコン設定のポップアップの中身</summary>
    void DrawSceneIconSettings();

    std::vector<SceneIcon> sceneIcons_;
    int pressedSceneIcon_ = -1;      // 押したときに下にあったアイコン（離した時も同じなら選ぶ）
    int hoveredSceneIcon_ = -1;      // マウスが乗っているアイコン
    std::string selectedCameraIcon_; // アイコンで選んだカメラ（ギズモ未登録なのでこちらで覚える）

    /// <summary>カメラのブックマーク（シーンごとに9つ）</summary>
    struct CameraBookmark
    {
        bool valid = false;
        Vector3 position = {0.0f, 0.0f, 0.0f};
        Vector3 rotation = {0.0f, 0.0f, 0.0f};
    };
    static constexpr int kCameraBookmarkCount = 9;
    void LoadCameraBookmarks();
    void SaveCameraBookmarks() const;
    void StoreCameraBookmark(int slot);
    void RecallCameraBookmark(int slot);
    void DrawCameraBookmarkPopup();
    void HandleCameraBookmarkKeys(bool sceneHovered);

    /// <summary>Undo 履歴の窓（ImGuiManagerTools.cpp）</summary>
    void ShowUndoHistoryWindow();

    /// <summary>配置ツールの窓（ImGuiManagerTools.cpp）</summary>
    void ShowPlacementToolWindow();

    /// <summary>カラーパレットの窓（ImGuiManagerTools.cpp）。色を貯めて、各所の色の欄へドラッグで渡す</summary>
    void ShowColorPaletteWindow();
    struct PaletteSwatch
    {
        Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        std::string name;
    };
    void LoadColorPalette();
    void SaveColorPalette() const;
#endif // USE_IMGUI

    // ゲームパラメータHub窓（コード登録済みパラメータを実行中に仕分け・調整する）
    void ShowGameParamWindow();

    void FixAspectRatio();

    void BackupDockLayout();

    void RestoreDockLayout();

    void SwitchToEditorMode();
    void SwitchToGameMode();
    void SaveCurrentLayout();
    void LoadLayoutForCurrentMode();
    void ShowHelpWindow();

    void SaveFlag();
    void LoadFlag();

#ifdef USE_IMGUI
    // ---- エディタの枠組み（ImGuiManagerShell.cpp）----

    /// <summary>ウィンドウ一覧（メニュー・コマンドパレット・ワークスペースが共通で使う）の1項目</summary>
    struct EditorWindowEntry
    {
        const char *id;       // ワークスペースから指すための識別名
        const char *icon;     // アイコン
        const char *label;    // 表示名
        const char *category; // メニューの見出し
        const char *tip;      // 説明
        bool *flag;           // 表示フラグ
    };

    /// <summary>ワークスペース（作業ごとのウィンドウの組み合わせ）</summary>
    struct EditorWorkspace
    {
        const char *name;
        const char *icon;
        const char *description;
        std::vector<const char *> windows; // 開くウィンドウの id（それ以外は閉じる）
        uint32_t pickMask;                 // シーンのクリック対象の既定（bit i = GizmoCategory i）
    };

    /// <summary>ウィンドウ一覧を作る（表示フラグのアドレスを覚えるので Initialize で1回だけ）</summary>
    void BuildWindowRegistry();

    /// <summary>ワークスペースの一覧を作る</summary>
    void BuildWorkspaces();

    /// <summary>表示メニューのウィンドウ一覧を描く</summary>
    void DrawWindowMenuItems();

    /// <summary>表示メニューの中身（検索・分類ごとの窓・開いている窓・ワークスペース・画面・エディタ）</summary>
    void DrawViewMenu();

    /// <summary>1つの窓のトグル行（右に分類を出すかどうか）</summary>
    void DrawWindowToggle(const EditorWindowEntry &entry, bool showCategory);

    /// <summary>表示メニューのグリッド設定サブメニュー</summary>
    void DrawGridMenu();

    /// <summary>表示メニューの画面（ゲーム/エディタ切替・フルスクリーン・解像度）</summary>
    void DrawScreenMenuItems();

    std::string viewMenuSearch_; // 表示メニューの窓の検索

    /// <summary>表示メニューのワークスペース一覧を描く</summary>
    void DrawWorkspaceMenuItems();

    /// <summary>ワークスペースを適用する（含まれる窓だけを開く）</summary>
    void ApplyWorkspace(int index);

    /// <summary>画面下のステータスバー（ドックより先に呼ぶこと）</summary>
    void DrawStatusBar();

    /// <summary>通知の履歴ウィンドウ</summary>
    void ShowNotificationWindow();

    /// <summary>外観の設定ウィンドウ</summary>
    void ShowAppearanceWindow();

    /// <summary>コマンドパレット（Ctrl+K）の開閉と描画</summary>
    void ShowCommandPalette();

    /// <summary>コマンドパレットの項目を組み立てる</summary>
    void BuildPaletteCommands(std::vector<EditorCommand> &out);

    /// <summary>外観の変更があればスタイルを作り直す（NewFrame の前に呼ぶ）</summary>
    void ApplyAppearanceIfDirty();

    std::vector<EditorWindowEntry> windowRegistry_;
    std::vector<EditorWorkspace> workspaces_;
    int currentWorkspace_ = -1; // 最後に適用したワークスペース（-1=なし）
    // ワークスペースごとのシーンのクリック対象。切り替えるときに今の状態を元の側へ覚えさせるので、
    // 手で変えた組み合わせは次にそのワークスペースへ戻ったときもそのまま使われる
    std::vector<uint32_t> workspacePickMasks_;
    std::unique_ptr<EditorCommandPalette> commandPalette_;
    std::unique_ptr<EditorAppearance> appearance_;
    std::unique_ptr<EditorAssetBrowser> assetBrowser_;
    // シーンにビヘイビアツリーで動くキャラがいないときに使う、ファイル編集だけのエディタ
    std::shared_ptr<BehaviorTreeEditor> standaloneBtEditor_;
    std::shared_ptr<AnimationStateMachineEditor> animStateMachineEditor_; // アニメーションのステートマシンのエディタ
    bool appearanceDirty_ = false;
    bool showNotificationHistoryPopup_ = false;
#endif // USE_IMGUI

  private:
    /// ====================================
    /// private variables
    /// ====================================

    std::string dockLayoutBackup_;

    // SRV用デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    SrvManager *pSrvManager_ = nullptr;
    BaseScene *pCurrentScene_ = nullptr;

    DirectXCommon *pDxCommon_;
#ifdef USE_IMGUI
    // ヒエラルキーウィンドウ
    ImVec2 hierarchyWindowPosition_ = {0.0f, 64.0f};

    // シーンウィンドウ
    ImVec2 sceneTextureSize_ = {800.0f, 450.0f};
    ImVec2 actualScenePos_ = {};  // ImGui座標系（ImGuizmo用）
    ImVec2 scenePosForRay_ = {};  // 仮想解像度座標系（レイ計算用）
    ImVec2 sceneSizeForRay_ = {}; // 仮想解像度座標系（レイ計算用）

#endif // USE_IMGUI
    // プリミティブごとの連番カウンタは廃止。
    // 名前の一意化は BaseObjectManager::MakeUniqueObjectName が行う
    // （カウンタ方式は読み込み後の既存名と衝突しうるため）。

    // エンジンのウィンドウを描画するフラグ
    // 重いUIコンポーネントの表示状態管理
    bool isShowMainUI_ = false;
    bool showSceneView_ = true;
    bool showObjectView_ = true;
    bool showParticleView_ = true;
    bool showParticlePreviewView_ = false; // GPUパーティクル プレビュー窓
    bool showFPSView_ = true;
    bool showOfScreenView_ = true;
    bool showLightView_ = true;
    bool isEditorMode_ = true;   // エディターモードフラグ
    bool multiViewport_ = false; // マルチビューポート有効フラグ
    bool showShortcutWindow_ = false;
    bool showGizmoView_ = true;
    bool showHierarchyView_ = true;
    bool showMotionEditorView_ = true;
    bool showBehaviorTreeView_ = false; // ビヘイビアツリーエディタ窓
    bool showAnimStateMachineView_ = false; // アニメーションのステートマシン窓
    bool showSpriteManagerView_ = true;
    bool focusTextSpriteTab_ = false; // 次に描くとき、スプライトマネージャの「文字から作る」タブを前に出す
    bool showUIEditorView_ = false; // UIエディタ窓
    bool showColliderTagManagerView_ = false;
    bool showAudioManagerView_ = false;
    bool showMusicEditorView_ = false; // 音楽エディタ窓
    bool showCaptureView_ = false;     // キャプチャ窓
    bool showConsoleView_ = false;     // コンソール窓
    bool showTimelineView_ = false;    // タイムライン窓
    bool showShadowMapView_ = true;
    bool showDrawSystemView_ = true;
    bool showShaderEditorView_ = false; // シェーダー窓（HLSLの閲覧・編集）
    bool showCameraView_ = false; // カメラ窓
    bool showGameParamView_ = true;     // ゲームパラメータHub窓
    bool showAssetBrowserView_ = false; // アセットブラウザ窓
    bool showNotificationView_ = false; // 通知の履歴窓
    bool showAppearanceView_ = false;   // 外観の設定窓
    bool showInspectorView_ = true;     // インスペクタ窓（選択中の物の詳細）
    bool showSceneOverlay_ = true;      // シーンビューのツールバー
    bool showViewAxis_ = true;          // シーンビュー右上の軸の向き表示
    bool showSceneIcons_ = true;        // シーンのアイコン（ライト・カメラ・エミッター・フィールド）
    float sceneIconScale_ = 1.0f;       // アイコンの大きさ
    int sceneIconKindMask_ = 0x1F;      // 種類ごとの表示（bit i = SceneIconKind i）
    unsigned int dockspaceId_ = 0;      // メインのドックスペース（新しい窓を初回にドックする先）
    bool showUndoHistoryView_ = false;  // Undo 履歴の窓
    bool showPlacementToolView_ = false; // 配置ツールの窓
    bool showColorPaletteView_ = false;  // カラーパレットの窓
    bool showDebugCameraView_ = false;   // デバッグカメラの窓
    bool showDebugLineView_ = false;     // デバッグ線の窓
    bool showInputActionView_ = false;   // 入力（キーコンフィグ）の窓
#ifdef USE_IMGUI
    std::vector<PaletteSwatch> paletteSwatches_;
    bool paletteLoaded_ = false;
#endif // USE_IMGUI
    int sceneLabelMode_ = 1;            // シーンの名前ラベル（0=出さない 1=選択中 2=すべて）
    bool showSceneMeasure_ = true;      // 2つ以上選んだときに距離を測って描く
    bool showThirdsGuide_ = false;      // 構図ガイド: 三分割線
    bool showSafeAreaGuide_ = false;    // 構図ガイド: セーフエリア（外周10%）
    std::string overlaySceneName_;      // ブックマークを引くためのシーン名
#ifdef USE_IMGUI
    std::map<std::string, std::array<CameraBookmark, kCameraBookmarkCount>> cameraBookmarks_;
    bool cameraBookmarksLoaded_ = false;
#endif // USE_IMGUI

    // グリッド設定用メンバ変数
    bool showGrid_ = true;
    float gridY_ = 0.0f;
    int gridDivision_ = 1000;
    float gridSize_ = 5000.0f;
    Vector4 gridColor_ = {0.5f, 0.5f, 0.5f, 1.0f}; // グレー

    // グリッドは形が変わらないので静的バッチとしてGPUへ常駐させる。
    // 分割数・サイズが変わったときだけ作り直し、Y座標と色は描画時に差し替える。
    LineBatchId gridBatch_ = kInvalidLineBatch;
    int builtGridDivision_ = -1;
    float builtGridSize_ = -1.0f;

    /// <summary>
    /// グリッドの静的バッチを必要なら作り直す
    /// </summary>
    void RebuildGridBatchIfNeeded();

    BaseObjectManager *pBaseObjectManager_ = nullptr;
    SpriteManager *pSpriteManager_ = nullptr;
    Audio *pAudio_ = nullptr;

    // ImGui レイアウト ini は AssetPath::ConfigRoot() 配下に生成する（プロジェクトルートを散らかさない）。
    std::string editorIniFilePath_;
    std::string gameIniFilePath_;
};
} // namespace Hagine
